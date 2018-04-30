#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "user_levent.h"
#include "user_udp.h"

static void random_string(char* s, const int len);
static void on_client_event(struct bufferevent* bev, short ev, void* arg);
static void on_listener_error(struct evconnlistener* l, void* arg);
static void on_accept(struct evconnlistener* l, evutil_socket_t fd,
                      struct sockaddr* addr, int socklen, void* arg);
static struct connection* make_connection(struct server* server, int id,
                                          struct sockaddr_in* addr);
static void               free_connection(struct connection* p);
static void free_all_connections(struct connection** p, int count);
static void server_free(struct server* p);
static void socket_set_nodelay(int fd);
static void on_connect(struct bufferevent* bev, short events, void* arg);

struct server*
server_new(struct client* base)
{
  struct server* p = malloc(sizeof(struct server));
  p->clients_count = 0;
  p->connections = NULL;
  p->listener = NULL;
  p->client = base;
  return p;
}

void
handle_sigint(int sig, short ev, void* arg)
{
  struct event_base* base = arg;
  event_base_loopbreak(base);
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

void
write_file(int fd, void* data, size_t size)
{
  if (fd < 0) {
    return;
  }
  int ret = write(fd, data, size);
  if (ret < 0) {
    perror("Failed to write the message to the device");
  }
}

void
open_file(struct client* c)
{
  char*  name = "/dev/paxos/klearner0";
  size_t strl = strlen(name) + 1;
  char*  fname = malloc(strl);
  memcpy(fname, name, strl);
  fname[strl - 2] = c->fileop.char_device_id + '0';
  c->fileop.fd = open(fname, O_RDWR | O_NONBLOCK, 0);
  if (c->fileop.fd < 0) {
    perror("Failed to open the device");
    free(c);
    exit(1);
  }
}

void
client_submit_value(struct client* c)
{
  struct client_value* v = (struct client_value*)c->ethop.send_buffer;
  v->client_id = c->id;
  gettimeofday(&v->t, NULL);
  v->size = c->value_size;

  /* ############################################
     HERE YOU SET THE VALUE TO SEND, YOU HAVE V->SIZE BYTES*/
  random_string(v->value, v->size);
  /* ############################################ */

  size_t size = sizeof(struct client_value) + v->size;
  udp_send_msg(c, v, size);
  // printf("Client: submitted PAXOS_CLIENT_VALUE %.16s\n", v->value);
}

int
client_connected(struct connection* p)
{
  return p->status == BEV_EVENT_CONNECTED;
}

static void
socket_set_nodelay(int fd)
{
  int flag = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int));
}

static void
on_client_event(struct bufferevent* bev, short ev, void* arg)
{
  struct connection* p = (struct connection*)arg;
  if (ev & BEV_EVENT_EOF || ev & BEV_EVENT_ERROR) {
    int                 i;
    struct connection** connections = p->server->connections;
    for (i = p->id; i < p->server->clients_count - 1; ++i) {
      connections[i] = connections[i + 1];
      connections[i]->id = i;
    }
    p->server->clients_count--;
    p->server->connections =
      realloc(p->server->connections,
              sizeof(struct connection*) * (p->server->clients_count));
    free_connection(p);
  } else {
    printf("Event %d not handled\n", ev);
  }
}

static void
on_listener_error(struct evconnlistener* l, void* arg)
{
  int                err = EVUTIL_SOCKET_ERROR();
  struct event_base* base = evconnlistener_get_base(l);
  printf("Listener error %d: %s. Shutting down event loop.\n", err,
         evutil_socket_error_to_string(err));
  event_base_loopexit(base, NULL);
}

static void
on_accept(struct evconnlistener* l, evutil_socket_t fd, struct sockaddr* addr,
          int socklen, void* arg)
{
  struct connection* client;
  struct server*     server = arg;

  server->connections =
    realloc(server->connections,
            sizeof(struct connection*) * (server->clients_count + 1));
  server->connections[server->clients_count] =
    make_connection(server, server->clients_count, (struct sockaddr_in*)addr);

  client = server->connections[server->clients_count];
  server->clients_count++;
  bufferevent_setfd(client->bev, fd);
  bufferevent_setcb(client->bev, on_read_sock, NULL, on_client_event, client);
  bufferevent_enable(client->bev, EV_READ | EV_WRITE);
  socket_set_nodelay(fd);
  printf("Accepted connection from %s:%d\n",
         inet_ntoa(((struct sockaddr_in*)addr)->sin_addr),
         ntohs(((struct sockaddr_in*)addr)->sin_port));
}

static struct connection*
make_connection(struct server* server, int id, struct sockaddr_in* addr)
{
  struct connection* p = malloc(sizeof(struct connection));
  p->id = id;
  p->addr = *addr;
  p->bev =
    bufferevent_socket_new(server->client->base, -1, BEV_OPT_CLOSE_ON_FREE);
  p->server = server;
  p->status = BEV_EVENT_EOF;
  return p;
}

int
server_listen(struct tcp_connection* tcp)
{
  struct sockaddr_in addr;
  unsigned           flags =
    LEV_OPT_CLOSE_ON_EXEC | LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE;

  /* listen on the given port at address 0.0.0.0 */
  memset(&addr, 0, sizeof(struct sockaddr_in));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(0);
  addr.sin_port = htons(tcp->dest_port);
  tcp->s->listener =
    evconnlistener_new_bind(tcp->s->client->base, on_accept, tcp->s, flags, -1,
                            (struct sockaddr*)&addr, sizeof(addr));
  if (tcp->s->listener == NULL) {
    printf("Failed to bind on port %d\n", tcp->dest_port);
    exit(1);
  }
  evconnlistener_set_error_cb(tcp->s->listener, on_listener_error);
  printf("Listening on port %d\n", tcp->dest_port);
  return 1;
}

static void
on_connect(struct bufferevent* bev, short events, void* arg)
{
  struct client* c = arg;

  if (events & BEV_EVENT_CONNECTED) {
    printf("Connected to server\n");
    bufferevent_write(c->tcpop.bev, &c->id, sizeof(int));
  } else {
    printf("ERROR: %s\n", evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
    // exit(1);
  }
}

struct bufferevent*
connect_to_server(struct client* c)
{
  struct bufferevent* bev;
  int                 flag = 1;
  struct sockaddr_in  addr =
    address_to_sockaddr(c->tcpop.dest_addr, c->tcpop.dest_port);

  bev = bufferevent_socket_new(c->base, -1, BEV_OPT_CLOSE_ON_FREE);
  bufferevent_setcb(bev, on_read_sock, NULL, on_connect, c);
  bufferevent_enable(bev, EV_READ | EV_WRITE);
  bufferevent_socket_connect(bev, (struct sockaddr*)&addr, sizeof(addr));
  setsockopt(bufferevent_getfd(bev), IPPROTO_TCP, TCP_NODELAY, &flag,
             sizeof(int));
  return bev;
}

void
usage(const char* name, int client)
{
  printf("Client Usage: %s [path/to/paxos.conf] [options] \n", name);
  printf("Options:\n");
  printf("  %-30s%s\n", "-h, --help", "Output this message and exit");
  printf("  %-30s%s\n", "-c, --char_device_id #", "Chardevice id");
  printf("  %-30s%s\n", "-m, --learner_port #", "Learner port (TCP)");
  if (client) {
    printf("  %-30s%s\n", "-l, --learner_addr #", "Learner address (TCP)");
    printf("  %-30s%s\n", "-i, --if_name #", "Interface name (ETH)");
    printf("  %-30s%s\n", "-p, --proposer-addr #", "Proposer address (ETH)");
    printf("  %-30s%s\n", "-o, --outstanding #",
           "Number of outstanding client values");
    printf("  %-30s%s\n", "-v, --value-size #",
           "Size of client value (in bytes)");
  }

  exit(1);
}

static void
free_connection(struct connection* p)
{
  bufferevent_free(p->bev);
  free(p);
}

static void
free_all_connections(struct connection** p, int count)
{
  int i;
  for (i = 0; i < count; i++)
    free_connection(p[i]);
  if (count > 0)
    free(p);
}

static void
server_free(struct server* p)
{
  free_all_connections(p->connections, p->clients_count);
  if (p->listener != NULL)
    evconnlistener_free(p->listener);
  free(p);
}

void
client_free(struct client* cl, int chardevice, int send, int sock)
{
  event_free(cl->sig);
  if (chardevice)
    event_free(cl->fileop.evread);

  if (send) {
    free(cl->ethop.send_buffer);
    event_free(cl->stats_ev);
    event_free(cl->ethop.resend_ev);
    close(cl->ethop.socket);
  }

  if (sock) {
    if (send)
      bufferevent_free(cl->tcpop.bev);
    else
      server_free(cl->tcpop.s);
  }
  event_base_free(cl->base);
  free(cl);
}
