#include "user_eth.h"
#include "user_levent.h"
#include "user_stats.h"
#include <getopt.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char           receive[ETH_DATA_LEN];
static int            use_chardevice = 0;
static struct server* server = NULL;
struct stats          stats; /* Statistics */
struct timeval        stats_interval;
struct event*         stats_ev;
unsigned long         count;

static void
unpack_message(char* msg, size_t len)
{
  struct user_msg*     mess = (struct user_msg*)msg;
  struct client_value* val = (struct client_value*)mess->value;
  struct server*       serv = server;
  for (int i = 0; i < serv->clients_count; i++) {
    if (val->client_id >= serv->connections[i]->start_id &&
        val->client_id < serv->connections[i]->end_id) {
      count++;
      bufferevent_write(serv->connections[i]->bev, msg, len);
      break;
    }
  }
}

static void
on_read_file(evutil_socket_t fd, short event, void* arg)
{
  struct event_base* base = arg;

  int len = read(server->fileop.fd, receive, ETH_DATA_LEN);
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
  struct connection* conn = (struct connection*)arg;
  char*              c = server->tcpop.rec_buffer;
  size_t             len = bufferevent_read(bev, c, ETH_DATA_LEN);

  if (!len) {
    return;
  }
  int* buff = (int*)c;
  conn->start_id = buff[0];
  conn->end_id = buff[1];
  bufferevent_write(conn->bev, buff, sizeof(int) * 2);
}

void
on_stats2(evutil_socket_t fd, short event, void* arg)
{

  printf("Learner: %lu value/sec\n", count);
  count = 0;
  event_add(stats_ev, &stats_interval);
}

static void
make_learner(struct server* cl)
{
  cl->base = event_base_new();
  if (server_listen(cl))
    goto cleanup;

  // chardevice
  if (open_file(&cl->fileop))
    goto cleanup;

  cl->fileop.evread = event_new(cl->base, cl->fileop.fd, EV_READ | EV_PERSIST,
                                on_read_file, cl->base);
  event_add(cl->fileop.evread, NULL);

  count = 0;
  stats_interval = (struct timeval){ 1, 0 };
  stats_ev = evtimer_new(cl->base, on_stats2, cl);
  event_add(stats_ev, &stats_interval);

  event_base_dispatch(cl->base);
cleanup:
  server_free(cl);
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
check_args(int argc, char* argv[], struct server* serv)
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
        serv->fileop.char_device_id = atoi(optarg);
        break;
      case 'm':
        serv->tcpop.dest_port = atoi(optarg);
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
  struct server* serv = server_new();
  server = serv;
  serv->tcpop.dest_port = 4000;

  check_args(argc, argv, serv);

  if (!use_chardevice) {
    printf("You must use chardevice\n");
    free(serv);
    usage(argv[0], 0);
    exit(1);
  }

  gettimeofday(&seed, NULL);
  srand(seed.tv_usec);

  make_learner(serv);
  signal(SIGPIPE, SIG_IGN);

  return 0;
}
