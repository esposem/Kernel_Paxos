#include "user_levent.h"
#include "user_stats.h"
#include "user_udp.h"
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char    receive[ETH_DATA_LEN];
static int     use_chardevice = 0, use_socket = 0;
struct client* client = NULL;
static void
unpack_message(char* msg)
{
  struct user_msg*     mess = (struct user_msg*)msg;
  struct client_value* val = (struct client_value*)mess->value;
  if (val->client_id == client->id) {
    update_stats(&client->stats, val->t, client->value_size);
    client_submit_value(client);
    event_add(client->ethop.resend_ev, &client->ethop.resend_interval);
  }
}

static void
on_read_file(evutil_socket_t fd, short event, void* arg)
{
  int           len;
  struct event* ev = arg;
  len = read(client->fileop.fd, receive, ETH_DATA_LEN);

  if (len < 0) {
    if (len == -2) {
      printf("Stopped by kernel module\n");
      event_del(ev);
      event_base_loopbreak(event_get_base(ev));
    }
    return;
  }
  unpack_message(receive);
}

void
on_read_sock(struct bufferevent* bev, void* arg)
{
  struct client* cl = (struct client*)arg;
  char*          c = cl->tcpop.rec_buffer;
  size_t         len = bufferevent_read(bev, c, ETH_DATA_LEN);

  if (!len) {
    return;
  }

  if (*c == 'k') { // ok
    for (size_t i = 0; i < cl->outstanding; ++i) {
      client_submit_value(cl);
    }
  } else {
    unpack_message(c);
  }
}

static void
on_resend(evutil_socket_t fd, short event, void* arg)
{
  struct client* cl = (struct client*)arg;
  client_submit_value(cl);
  event_add(cl->ethop.resend_ev, &cl->ethop.resend_interval);
}

static void
make_client(struct client* cl)
{
  struct event_config* cfg = event_config_new();
  event_config_avoid_method(cfg, "epoll");
  cl->base = event_base_new_with_config(cfg);
  cl->id = rand();
  printf("id is %d\n", cl->id);

  // TCP socket to connect learner
  if (use_socket) {
    cl->tcpop.bev = connect_to_server(cl);
    if (cl->tcpop.bev == NULL) {
      printf("Could not start TCP connection\n");
      exit(1);
    }
  } else { // chardevice
    open_file(cl);
    // size_t s = sizeof(struct client_value) + cl->value_size;
    // write_file(cl->fileop.fd, &s, sizeof(size_t));
    cl->fileop.evread = event_new(cl->base, cl->fileop.fd, EV_READ | EV_PERSIST,
                                  on_read_file, NULL);
    event_add(cl->fileop.evread, NULL);
  }

  // stop with ctrl+c
  cl->sig = evsignal_new(cl->base, SIGINT, handle_sigint, cl->base);
  evsignal_add(cl->sig, NULL);

  // ethernet for proposer
  cl->ethop.send_buffer = malloc(sizeof(struct client_value) + cl->value_size);
  init_socket(cl);

  // print statistic every 1 sec
  cl->stats_interval = (struct timeval){ 1, 0 };
  cl->ethop.resend_interval = (struct timeval){ 1, 0 };
  cl->stats_ev = evtimer_new(cl->base, on_stats, cl);
  event_add(cl->stats_ev, &cl->stats_interval);

  // resend value after 1 sec I did not receive anything
  cl->ethop.resend_ev = evtimer_new(cl->base, on_resend, cl);
  event_add(cl->ethop.resend_ev, &cl->ethop.resend_interval);

  for (size_t i = 0; i < cl->outstanding; i++) {
    client_submit_value(cl);
  }

  event_base_dispatch(cl->base);
  client_free(cl, use_chardevice, 1, use_socket);
  event_config_free(cfg);
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
    { "help", no_argument, 0, 'h' },
    { 0, 0, 0, 0 }
  };

  while ((opt = getopt_long(argc, argv, "i:p:c:l:m:v:o:h", options, &idx)) !=
         -1) {
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

int
main(int argc, char* argv[])
{
  struct timeval seed;
  struct client* cl = valloc(sizeof(struct client));
  client = cl;
  cl->ethop.if_name = "enp0s3";
  cl->tcpop.dest_addr = "127.0.0.1";
  cl->tcpop.dest_port = 4000;
  for (int i = 0; i < ETH_ALEN; ++i)
    cl->ethop.prop_addr[i] = 0x0;
  cl->value_size = 64;
  cl->outstanding = 1;

  check_args(argc, argv, cl);
  printf("if_name %s\n", cl->ethop.if_name);
  char a[20];
  mac_to_str(cl->ethop.prop_addr, a);
  printf("address %s\n", a);

  if ((use_chardevice ^ use_socket) == 0) {
    printf("Either use chardevice or connect remotely to a learner\n");
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
