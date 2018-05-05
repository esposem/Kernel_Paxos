#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "user_eth.h"
#include "user_levent.h"

/* ################## common methods ################ */
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
open_file(struct chardevice* c)
{
  char*  name = "/dev/paxos/klearner0";
  size_t strl = strlen(name) + 1;
  char*  fname = malloc(strl);
  memcpy(fname, name, strl);
  fname[strl - 2] = c->char_device_id + '0';
  c->fd = open(fname, O_RDWR | O_NONBLOCK, 0);
  if (c->fd < 0) {
    perror("Failed to open the device");
    exit(1);
  }
}

struct sockaddr_in
address_to_sockaddr(const char* ip, int port)
{
  struct sockaddr_in addr;

  memset(&addr, 0, sizeof(struct sockaddr_in));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = inet_addr(ip);
  return addr;
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
    printf("  %-30s%s\n", "-d, --id #", "Starting id");
    printf("  %-30s%s\n", "-n, --nclients #",
           "Number of virtual clients to simulate");
  }

  exit(1);
}

/* ################## Server ################ */

struct server*
server_new()
{
  struct server* p = malloc(sizeof(struct server));
  memset(p, 0, sizeof(struct server));
  p->clients_count = 0;
  p->connections = NULL;
  p->listener = NULL;
  return p;
}

static struct connection*
make_connection(struct server* server, int id, struct sockaddr_in* addr)
{
  struct connection* p = malloc(sizeof(struct connection));
  p->id = id;
  p->addr = *addr;
  p->bev = bufferevent_socket_new(server->base, -1, BEV_OPT_CLOSE_ON_FREE);
  p->server = server;
  p->status = BEV_EVENT_EOF;
  p->start_id = -1;
  p->end_id = -1;
  return p;
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
  for (int i = 0; i < count; i++)
    free_connection(p[i]);
  if (count > 0)
    free(p);
}

void
server_free(struct server* p)
{
  free_all_connections(p->connections, p->clients_count);
  if (p->listener != NULL)
    evconnlistener_free(p->listener);
  free(p);
  event_free(p->fileop.evread);
  // event_free(p->sig);
  bufferevent_free(p->tcpop.bev);
  event_base_free(p->base);
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
    struct connection** connections = p->server->connections;
    for (int i = p->id; i < p->server->clients_count - 1; ++i) {
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

static void
on_listener_error(struct evconnlistener* l, void* arg)
{
  int                err = EVUTIL_SOCKET_ERROR();
  struct event_base* base = evconnlistener_get_base(l);
  printf("Listener error %d: %s. Shutting down event loop.\n", err,
         evutil_socket_error_to_string(err));
  event_base_loopexit(base, NULL);
}

int
server_listen(struct server* serv)
{
  struct sockaddr_in addr;
  unsigned           flags =
    LEV_OPT_CLOSE_ON_EXEC | LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE;

  /* listen on the given port at address 0.0.0.0 */
  memset(&addr, 0, sizeof(struct sockaddr_in));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(0);
  addr.sin_port = htons(serv->tcpop.dest_port);
  serv->listener =
    evconnlistener_new_bind(serv->base, on_accept, serv, flags, -1,
                            (struct sockaddr*)&addr, sizeof(addr));
  if (serv->listener == NULL) {
    printf("Failed to bind on port %d\n", serv->tcpop.dest_port);
    exit(1);
  }
  evconnlistener_set_error_cb(serv->listener, on_listener_error);
  printf("Listening on port %d\n", serv->tcpop.dest_port);
  return 1;
}

/* ################## Client ################ */

void
handle_sigint(int sig, short ev, void* arg)
{
  struct event_base* base = arg;
  printf("Stop!\n");
  event_base_loopbreak(base);
}

void
client_free(struct client* cl, int chardevice, int sock)
{
  event_free(cl->sig);
  if (chardevice)
    event_free(cl->fileop.evread);

  free(cl->ethop.send_buffer);
  event_free(cl->stats_ev);
  close(cl->ethop.socket);
  free(cl->nclients_time);

  if (sock) {
    bufferevent_free(cl->tcpop.bev);
  }
  event_base_free(cl->base);
  free(cl);
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
client_submit_value(struct client* c, int id)
{
  struct client_value* v = (struct client_value*)c->ethop.send_buffer;
  v->client_id = id;

  v->size = c->value_size;
  /* ############################################ */
  random_string(v->value, v->size);
  /* ############################################ */
  size_t size = sizeof(struct client_value) + v->size;

  gettimeofday(&c->nclients_time[id - c->id], NULL);
  v->t = c->nclients_time[id - c->id];
  eth_sendmsg(c, v, size);
  // printf("Client %d submitted value %.16s with %zu bytes\n", v->client_id,
  //        v->value, v->size);
}

static void
on_connect(struct bufferevent* bev, short events, void* arg)
{
  struct client* c = arg;

  if (events & BEV_EVENT_CONNECTED) {
    printf("Connected to server\n");
    int buff[2];
    buff[0] = c->id;               // starting id
    buff[1] = c->nclients + c->id; // ending id (not included)
    bufferevent_write(c->tcpop.bev, buff, sizeof(buff));
  } else {
    printf("ERROR: %s\n", evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
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
