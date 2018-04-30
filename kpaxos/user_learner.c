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
  if (use_socket) {
    size_t         i;
    struct server* serv = client->tcpop.s;
    for (i = 0; i < serv->clients_count; i++) {
      if (serv->connections[i]->cl_id == val->client_id) {
        size_t total_size = sizeof(struct user_msg) +
                            sizeof(struct client_value) + client->value_size;
        bufferevent_write(serv->connections[i]->bev, msg, total_size);
        break;
      }
    }
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
  struct connection* conn = (struct connection*)arg;
  char*              c = client->tcpop.rec_buffer;
  char               ok = 'k';
  size_t             len = bufferevent_read(bev, c, ETH_DATA_LEN);

  if (!len) {
    return;
  }

  memcpy(&conn->cl_id, c, sizeof(int));
  bufferevent_write(conn->bev, &ok, 1);
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
    cl->tcpop.s = server_new(cl);
    if (cl->tcpop.s == NULL) {
      printf("Could not start TCP connection\n");
      exit(1);
    }
    server_listen(&cl->tcpop);
  }

  // chardevice
  open_file(cl);
  // size_t s = sizeof(struct client_value) + cl->value_size;
  // write_file(cl->fileop.fd, &s, sizeof(size_t));
  cl->fileop.evread = event_new(cl->base, cl->fileop.fd, EV_READ | EV_PERSIST,
                                on_read_file, NULL);
  event_add(cl->fileop.evread, NULL);

  // stop with ctrl+c
  cl->sig = evsignal_new(cl->base, SIGINT, handle_sigint, cl->base);
  evsignal_add(cl->sig, NULL);

  event_base_dispatch(cl->base);
  client_free(cl, use_chardevice, 0, use_socket);
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
    { "char_device_id", required_argument, 0, 'c' },
    { "learner_port", required_argument, 0, 'm' },
    { "help", no_argument, 0, 'h' },
    { 0, 0, 0, 0 }
  };

  while ((opt = getopt_long(argc, argv, "c:m:h", options, &idx)) != -1) {
    switch (opt) {
      case 'c':
        use_chardevice = 1;
        cl->fileop.char_device_id = atoi(optarg);
        break;
      case 'm':
        use_socket = 1;
        cl->tcpop.dest_port = atoi(optarg);
        break;
      default:
        usage(argv[0], 0);
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
  cl->tcpop.dest_port = 4000;

  check_args(argc, argv, cl);

  if (!use_chardevice) {
    printf("You must use chardevice\n");
    free(cl);
    usage(argv[0], 0);
    exit(1);
  }

  gettimeofday(&seed, NULL);
  srand(seed.tv_usec);

  make_client(cl);
  signal(SIGPIPE, SIG_IGN);

  return 0;
}
