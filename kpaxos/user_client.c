#include "user_eth.h"
#include "user_levent.h"
#include "user_stats.h"
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int            use_chardevice = 0, use_socket = 0;
static int            stop = 1;
static struct client* client;

void
stop_execution(int sig)
{
  if (stop) {
    int buff[2];
    buff[0] = CLOSE_CONN;
    buff[1] = client->learner_id;
    eth_sendmsg(&client->ethop, client->learner_addr, buff, sizeof(buff));
  }
  stop = 0;
}

void
timercallback(int sig)
{
  on_stats(client);
  if (stop)
    alarm(1);
}

static void
unpack_message(struct client* cl, ssize_t len)
{
  struct user_msg*     mess = (struct user_msg*)cl->ethop.rec_buffer;
  struct client_value* val = (struct client_value*)mess->value;
  int                  id = val->client_id - cl->id;

  if (id >= 0 && id < cl->nclients) {
    update_stats(&cl->stats, val->t, cl->value_size);
    // printf("Client %d received value %.16s with %zu bytes\n", val->client_id,
    //        val->value, val->size);
    client_submit_value(cl, val->client_id);
  }
}

static void
read_file(struct client* cl)
{

  ssize_t len = read(cl->fileop.fd, cl->ethop.rec_buffer, ETH_DATA_LEN);
  if (len == 0) {
    printf("Stopped by kernel module\n");
    stop = 0;
  }
  unpack_message(cl, len);
}

void
read_socket(struct client* cl)
{
  int*       buff = (int*)cl->ethop.rec_buffer;
  static int set_learner = 1;
  ssize_t    len;

  len = eth_recmsg(&cl->ethop, NULL, cl->ethop.rec_buffer, ETH_DATA_LEN);
  if (len <= 0)
    return;

  if (set_learner && buff[0] == OK) {
    cl->learner_id = buff[1];
    printf("Connected with learner! id %d\n", buff[1]);
    for (int i = 0; i < cl->nclients; i++) {
      client_submit_value(cl, i + cl->id);
    }
    set_learner = 0;
    return;
  }
  unpack_message(cl, len);
}

static void
make_client(struct client* cl)
{

  struct pollfd  pol;
  struct timeval seed;
  int            fileid;
  char           file_name[128];
  void (*callback)(struct client * cl);

  // ethernet for proposer
  if (eth_init(&cl->ethop))
    goto cleanup;

  if (eth_listen(&cl->ethop))
    goto cleanup;

  pol.events = POLLIN;

  if (use_socket) {
    int buff[3];
    buff[0] = OPEN_CONN;
    buff[1] = cl->id;
    buff[2] = cl->nclients + cl->id;
    eth_sendmsg(&cl->ethop, cl->learner_addr, buff, sizeof(buff));
    pol.fd = cl->ethop.socket;
    callback = read_socket;
  } else { // chardevice
    if (open_file(&cl->fileop))
      goto cleanup;

    for (int i = 0; i < cl->nclients; i++) {
      client_submit_value(cl, i + cl->id);
    }
    pol.fd = cl->fileop.fd;
    callback = read_file;
  }
  stats_init();

  while (stop) {
    poll(&pol, 1, -1);
    if (pol.revents == POLLIN) {
      callback(cl);
    }
  }

  stats_print();
  gettimeofday(&seed, NULL);
  srand(seed.tv_usec);
  fileid = rand();
  fileid &= 0xffffff;
  sprintf(file_name, "stats-%3.3dclients-%d.txt", cl->nclients, fileid);
  stats_persist(file_name);
  stats_destroy();

cleanup:
  client_free(cl);
}

static void
check_args(int argc, char* argv[], struct client* cl, char** path)
{
  int opt = 0, idx = 0;

  static struct option options[] = {
    { "if_name", required_argument, 0, 'i' },
    { "proposer-id", required_argument, 0, 'p' },
    { "learner-addr", required_argument, 0, 'l' },
    { "chardev_id", required_argument, 0, 'c' },
    { "value-size", required_argument, 0, 'v' },
    { "outstanding", required_argument, 0, 'o' },
    { "id", required_argument, 0, 'd' },
    { "nclients", required_argument, 0, 'n' },
    { "file", required_argument, 0, 'f' },
    { "help", no_argument, 0, 'h' },
    { 0, 0, 0, 0 }
  };

  while ((opt = getopt_long(argc, argv, "i:p:c:l:v:o:d:n:f:h", options,
                            &idx)) != -1) {
    switch (opt) {
      case 'i':
        cl->ethop.if_name = optarg;
        break;
      case 'p':
        cl->prop_id = atoi(optarg);
        break;
      case 'c':
        use_chardevice = 1;
        cl->fileop.char_device_id = atoi(optarg);
        break;
      case 'l':
        use_socket = 1;
        str_to_mac(optarg, cl->learner_addr);
        break;
      case 'v':
        cl->value_size = atoi(optarg);
        break;
      case 'o':
        cl->outstanding = atoi(optarg);
        break;
      case 'd':
        cl->id = atoi(optarg);
        break;
      case 'n':
        cl->nclients = atoi(optarg);
        break;
      case 'f':
        *path = optarg;
        break;
      default:
        usage(argv[0], 1);
    }
  }
}

int
main(int argc, char* argv[])
{
  struct client* cl = client_new();
  char*          path = "./paxos.conf";

  client = cl;

  check_args(argc, argv, cl, &path);
  cl->nclients_time = malloc(sizeof(struct timeval) * cl->nclients);
  memset(cl->nclients_time, 0, sizeof(struct timeval) * cl->nclients);
  cl->send_buffer_len = sizeof(struct client_value) + cl->value_size;

  prepare_clval(cl);

  if (find_proposer(cl, path)) {
    printf("Wrong proposer id %d\n", cl->prop_id);
    goto exit_err;
  }
  print_settings(cl);

  if ((use_chardevice ^ use_socket) == 0) {
    printf("Either use chardevice or connect remotely to a learner\n");
    goto exit_err;
  }

  signal(SIGINT, stop_execution);
  signal(SIGALRM, timercallback);
  alarm(1);
  make_client(cl);

  return 0;

exit_err:
  free(cl->nclients_time);
  free(cl);
  usage(argv[0], 1);
  exit(1);
}
