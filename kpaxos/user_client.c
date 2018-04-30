#include "user_levent.h"
#include "user_stats.h"
#include "user_udp.h"
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char    receive[BUFFER_LENGTH];
struct client* cl;
static int     use_chardevice = 0, use_socket = 0, outstanding = 1;
static int     learner_id = 0, proposer_id = 0, value_size = 64;
static char*   dest_addr = "00:00:00:00:00:00";

static void
unpack_message(char* msg)
{
  struct user_msg*     mess = (struct user_msg*)msg;
  struct client_value* val = (struct client_value*)mess->value;
  if (val->client_id == cl->id) {
    update_stats(&cl->stats, val->t, cl->value_size);
    client_submit_value(cl);
    event_add(cl->resend_ev, &cl->resend_interval);
  }
}

static void
on_read_file(evutil_socket_t fd, short event, void* arg)
{
  int           len;
  struct event* ev = arg;
  len = read(cl->fd, receive, BUFFER_LENGTH);

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
  char*  c = malloc(BUFFER_LENGTH);
  char   ok = 'k';
  size_t len = bufferevent_read(bev, c, BUFFER_LENGTH);

  if (!len) {
    goto freec;
  }

  if (memcmp(c, &ok, 1) == 0) {
    size_t i;
    for (i = 0; i < outstanding; i++) {
      client_submit_value(cl);
    }
  } else {
    unpack_message(c);
  }

freec:
  free(c);
}

static void
on_resend(evutil_socket_t fd, short event, void* arg)
{
  client_submit_value(cl);
  event_add(cl->resend_ev, &cl->resend_interval);
}

static void
make_client(int proposer_id, int value_size)
{
  struct client* c;
  c = malloc(sizeof(struct client));
  if (c == NULL) {
    return;
  }
  cl = c;
  struct event_config* cfg = event_config_new();
  event_config_avoid_method(cfg, "epoll");
  c->base = event_base_new_with_config(cfg);
  c->id = rand();
  c->value_size = value_size;
  printf("id is %d\n", c->id);

  // TCP socket to connect learner
  if (use_socket) {
    c->bev = connect_to_server(c, dest_addr, dest_port);
    if (c->bev == NULL) {
      printf("Could not start TCP connection\n");
      exit(1);
    }
  } else { // chardevice
    open_file(c, learner_id);
    size_t s = sizeof(struct client_value) + value_size;
    write_file(c->fd, &s, sizeof(size_t));
    cl->evread =
      event_new(cl->base, cl->fd, EV_READ | EV_PERSIST, on_read_file, NULL);
    event_add(cl->evread, NULL);
  }

  // stop with ctrl+c
  c->sig = evsignal_new(c->base, SIGINT, handle_sigint, c->base);
  evsignal_add(c->sig, NULL);

  // ethernet for proposer
  c->send_buffer = malloc(sizeof(struct client_value) + c->value_size);
  init_socket(c);

  // print statistic every 1 sec
  c->stats_interval = (struct timeval){ 1, 0 };
  c->resend_interval = (struct timeval){ 1, 0 };
  c->stats_ev = evtimer_new(c->base, on_stats, c);
  event_add(c->stats_ev, &c->stats_interval);

  // resend value after 1 sec I did not receive anything
  c->resend_ev = evtimer_new(c->base, on_resend, NULL);
  event_add(c->resend_ev, &c->resend_interval);

  for (size_t i = 0; i < outstanding; i++) {
    client_submit_value(cl);
  }

  event_base_dispatch(c->base);
  client_free(c, use_chardevice, 1, use_socket);
  event_config_free(cfg);
}

void
check_args(int argc, char* argv[])
{
  int                  opt = 0, idx = 0;
  static struct option options[] = {
    { "dest-addr", required_argument, 0, 's' },
    { "device_id", required_argument, 0, 'd' },
    { "proposer-id", required_argument, 0, 'p' },
    { "value-size", required_argument, 0, 'v' },
    { "outstanding", required_argument, 0, 'o' },
    { "help", no_argument, 0, 'h' },
    { 0, 0, 0, 0 }
  };

  while ((opt = getopt_long(argc, argv, "s:d:p:v:o:h", options, &idx)) != -1) {
    switch (opt) {
      case 's':
        use_socket = 1;
        dest_addr = optarg;
        break;
      case 'd':
        use_chardevice = 1;
        learner_id = atoi(optarg);
        break;
      case 'p':
        proposer_id = atoi(optarg);
        break;
      case 'v':
        value_size = atoi(optarg);
        break;
      case 'o':
        outstanding = atoi(optarg);
        break;
      default:
        usage(argv[0]);
    }
  }
}

int
main(int argc, char* argv[])
{
  struct timeval seed;
  check_args(argc, argv);

  if ((use_chardevice ^ use_socket) == 0) {
    printf("Either use chardevice or connect remotely to a learner\n");
    usage(argv[0]);
    exit(1);
  }

  gettimeofday(&seed, NULL);
  srand(seed.tv_usec);

  make_client(proposer_id, value_size);
  signal(SIGPIPE, SIG_IGN);
  // libevent_global_shutdown();

  return 0;
}
