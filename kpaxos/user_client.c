#include "user_eth.h"
#include "user_levent.h"
#include "user_stats.h"
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char    receive[ETH_DATA_LEN];
static int     use_chardevice = 0, use_socket = 0;
struct client* client = NULL;
static int     expected_size = -1;

static void
unpack_message(char* msg, size_t len)
{
  struct user_msg*     mess = (struct user_msg*)msg;
  struct client_value* val = (struct client_value*)mess->value;
  int                  id = val->client_id - client->id;
  if (id >= 0 && id < client->nclients) {
    update_stats(&client->stats, val->t, client->value_size);
    // printf("Client %d received value %.16s with %zu bytes\n", val->client_id,
    //        val->value, val->size);
    client_submit_value(client, val->client_id);
  }
}

static void
on_read_file(evutil_socket_t fd, short event, void* arg)
{
  int                len;
  struct event_base* base = arg;
  len = read(client->fileop.fd, receive, ETH_DATA_LEN);

  if (len < 0)
    return;

  if (len == 0) {
    printf("Stopped by kernel module\n");
    event_base_loopbreak(base);
    return;
  }
  unpack_message(receive, len);
}

void
on_read_sock(struct bufferevent* bev, void* arg)
{
  struct client* cl = (struct client*)arg;
  char*          c = cl->tcpop.rec_buffer;
  int            len = bufferevent_read(bev, c, ETH_DATA_LEN);
  if (len <= 0)
    return;

  static int first_mess = 1;
  int*       buff = (int*)c;

  if (first_mess) {
    if (buff[0] == cl->id && buff[1] == ((cl->id + cl->nclients))) {
      for (int i = 0; i < cl->nclients; i++) {
        client_submit_value(cl, i + cl->id);
      }
      first_mess = 0;
      return;
    }
  }

  int i = 0;
  while (i < len) {
    unpack_message(c + i, expected_size);
    i += expected_size;
  }
}

static void
make_client(struct client* cl)
{
  cl->base = event_base_new();

  // stop with signint
  cl->sig = evsignal_new(cl->base, SIGINT, handle_sigint, cl->base);
  evsignal_add(cl->sig, NULL);

  // ethernet for proposer
  if (eth_init(cl))
    goto cleanup;

  // TCP socket to connect learner
  if (use_socket) {
    cl->tcpop.bev = connect_to_server(cl);
    if (cl->tcpop.bev == NULL) {
      printf("Could not start TCP connection\n");
      goto cleanup;
    }
  } else { // chardevice
    if (open_file(&cl->fileop))
      goto cleanup;
    cl->fileop.evread = event_new(cl->base, cl->fileop.fd, EV_READ | EV_PERSIST,
                                  on_read_file, cl->base);
    event_add(cl->fileop.evread, NULL);

    for (int i = 0; i < cl->nclients; i++) {
      client_submit_value(cl, i + cl->id);
    }
  }

  // print statistic every 1 sec
  cl->stats_interval = (struct timeval){ 1, 0 };
  cl->stats_ev = evtimer_new(cl->base, on_stats, cl);
  event_add(cl->stats_ev, &cl->stats_interval);

  event_base_dispatch(cl->base);
cleanup:
  client_free(cl, use_chardevice, use_socket);
}

int
str_to_mac(const char* str, uint8_t daddr[ETH_ALEN])
{
  int values[6], i;
  if (6 == sscanf(str, "%x:%x:%x:%x:%x:%x", &values[0], &values[1], &values[2],
                  &values[3], &values[4], &values[5])) {
    /* convert to uint8_t */
    for (i = 0; i < 6; ++i)
      daddr[i] = (uint8_t)values[i];
    return 1;
  }
  return 0;
}

static void
check_args(int argc, char* argv[], struct client* cl)
{
  int opt = 0, idx = 0;

  static struct option options[] = {
    { "if_name", required_argument, 0, 'i' },
    { "proposer-addr", required_argument, 0, 'p' },
    { "char_device_id", required_argument, 0, 'c' },
    { "learner_addr", required_argument, 0, 'l' },
    { "learner_port", required_argument, 0, 'm' },
    { "value-size", required_argument, 0, 'v' },
    { "outstanding", required_argument, 0, 'o' },
    { "id", required_argument, 0, 'd' },
    { "nclients", required_argument, 0, 'n' },
    { "help", no_argument, 0, 'h' },
    { 0, 0, 0, 0 }
  };

  while ((opt = getopt_long(argc, argv, "i:p:c:l:m:v:o:d:n:h", options,
                            &idx)) != -1) {
    switch (opt) {
      case 'i':
        cl->ethop.if_name = optarg;
        break;
      case 'p':
        str_to_mac(optarg, cl->ethop.prop_addr);
        break;
      case 'c':
        use_chardevice = 1;
        cl->fileop.char_device_id = atoi(optarg);
        break;
      case 'l':
        use_socket = 1;
        cl->tcpop.dest_addr = optarg;
        break;
      case 'm':
        use_socket = 1;
        cl->tcpop.dest_port = atoi(optarg);
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
      default:
        usage(argv[0], 1);
    }
  }
}

int
mac_to_str(uint8_t daddr[ETH_ALEN], char* str)
{
  sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x\n", daddr[0], daddr[1], daddr[2],
          daddr[3], daddr[4], daddr[5]);
  return 1;
}

static void
random_string(char* s, const int len)
{
  int               i;
  static const char alphanum[] = "0123456789abcdefghijklmnopqrstuvwxyz";
  for (i = 0; i < len - 1; ++i)
    s[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
  s[len - 1] = 0;
}

int
main(int argc, char* argv[])
{
  struct timeval seed;
  struct client* cl = malloc(sizeof(struct client));
  memset(cl, 0, sizeof(struct client));
  client = cl;
  cl->ethop.if_name = "enp0s3";
  cl->tcpop.dest_addr = "127.0.0.1";
  cl->tcpop.dest_port = 4000;
  for (int i = 0; i < ETH_ALEN; ++i)
    cl->ethop.prop_addr[i] = 0x0;
  cl->value_size = 64;
  cl->outstanding = 1;
  cl->nclients = 1;

  check_args(argc, argv, cl);
  cl->nclients_time = malloc(sizeof(struct timeval) * cl->nclients);
  memset(cl->nclients_time, 1, sizeof(struct timeval) * cl->nclients);
  cl->ethop.send_buffer_len = sizeof(struct client_value) + cl->value_size;
  cl->ethop.val = (struct client_value*)cl->ethop.send_buffer;
  cl->ethop.val->size = cl->value_size;
  expected_size = sizeof(struct user_msg) + cl->ethop.send_buffer_len;
  random_string(cl->ethop.val->value, cl->value_size);

  printf("if_name %s\n", cl->ethop.if_name);
  char a[20];
  mac_to_str(cl->ethop.prop_addr, a);
  printf("address %s", a);
  printf("id: %d ------ nclients: %d\n", cl->id, cl->nclients);
  printf("dest_addr: %s dest_port: %d\n", cl->tcpop.dest_addr,
         cl->tcpop.dest_port);

  if ((use_chardevice ^ use_socket) == 0) {
    printf("Either use chardevice or connect remotely to a learner\n");
    free(cl->nclients_time);
    free(cl);
    usage(argv[0], 1);
    exit(1);
  }

  gettimeofday(&seed, NULL);
  srand(seed.tv_usec);

  make_client(cl);
  signal(SIGPIPE, SIG_IGN);

  return 0;
}
