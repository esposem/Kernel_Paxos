#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <stdatomic.h>

#include "user_levent.h"
#include "user_udp.h"

static void random_string(char *s, const int len);
static void on_client_event(struct bufferevent *bev, short ev, void *arg);
static void on_listener_error(struct evconnlistener *l, void *arg);
static void on_accept(struct evconnlistener *l, evutil_socket_t fd, struct sockaddr *addr, int socklen, void *arg);
static struct connection *make_connection(struct server *server, int id,struct sockaddr_in *addr);
static void free_connection(struct connection *p);
static void free_all_connections(struct connection **p, int count);
static void server_free(struct server *p);
static void socket_set_nodelay(int fd);
static void on_connect(struct bufferevent *bev, short events, void *arg);

struct server *server_new(struct client *base) {
  struct server *p = malloc(sizeof(struct server));
  p->clients_count = 0;
  p->connections = NULL;
  p->listener = NULL;
  p->client = base;
  return p;
}


void handle_sigint(int sig, short ev, void* arg){
	struct event_base* base = arg;
	event_base_loopbreak(base);
}


static void random_string(char *s, const int len){
	int i;
	static const char alphanum[] =
		"0123456789abcdefghijklmnopqrstuvwxyz";
	for (i = 0; i < len-1; ++i)
		s[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
	s[len-1] = 0;
}

void write_file(int fd, void * data, int flag, size_t size){
	if(fd >= 0){
    char string[size+1];
    string[0] = flag + '0';
    memcpy(string+1, data, size);
		int ret = write(fd, string, size + 1);
		if (ret < 0){
			perror("Failed to write the message to the device");
		}
	}
}


void open_file(struct client * c){
	if(learner_id < 0){
		printf("Error: insert learner id > 0\n");
		free(c);
		exit(1);
	}
	char * def_name = "/dev/chardevice/klearner0";
	size_t strl = strlen(def_name) + 1;
	char * filename = malloc(strl);
	memcpy(filename, def_name, strl);
	filename[strl-2] = learner_id + '0';
  c->fd = open(filename, O_RDWR | O_NONBLOCK, 0);
   if (c->fd < 0){
    perror("Failed to open the device");
		free(c);
		exit(1);
  }
}


void client_submit_value(struct client* c) {
	struct client_value* v = (struct client_value*)c->send_buffer;
	v->client_id = c->id;
	gettimeofday(&v->t, NULL);
	v->size = c->value_size;

	/* ############################################
	   HERE YOU SET THE VALUE TO SEND, YOU HAVE V->SIZE BYTES*/
	random_string(v->value, v->size);
	/* ############################################ */

	size_t size = sizeof(struct client_value) + v->size;
	udp_send_msg(v, size);
	// printf("Client: submitted PAXOS_CLIENT_VALUE %.16s\n", v->value);
}


int client_connected(struct connection *p) {
  return p->status == BEV_EVENT_CONNECTED;
}


static void socket_set_nodelay(int fd) {
  int flag = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int));
}

static void on_client_event(struct bufferevent *bev, short ev, void *arg) {
  struct connection *p = (struct connection *)arg;
  if (ev & BEV_EVENT_EOF || ev & BEV_EVENT_ERROR) {
    int i;
    struct connection **connections = p->server->connections;
    for (i = p->id; i < p->server->clients_count - 1; ++i) {
      connections[i] = connections[i + 1];
      connections[i]->id = i;
    }
    p->server->clients_count--;
    p->server->connections =
        realloc(p->server->connections,
                sizeof(struct connection *) * (p->server->clients_count));
    free_connection(p);
  } else {
    printf("Event %d not handled\n", ev);
  }
}


static void on_listener_error(struct evconnlistener *l, void *arg) {
  int err = EVUTIL_SOCKET_ERROR();
  struct event_base *base = evconnlistener_get_base(l);
  printf("Listener error %d: %s. Shutting down event loop.\n", err,
         evutil_socket_error_to_string(err));
  event_base_loopexit(base, NULL);
}


static void on_accept(struct evconnlistener *l, evutil_socket_t fd,
                      struct sockaddr *addr, int socklen, void *arg) {
  struct connection *client;
  struct server *server = arg;

  server->connections = realloc(server->connections, sizeof(struct connection *) *
                                                 (server->clients_count + 1));
  server->connections[server->clients_count] =
      make_connection(server, server->clients_count, (struct sockaddr_in *)addr);

  client = server->connections[server->clients_count];
  bufferevent_setfd(client->bev, fd);
  bufferevent_setcb(client->bev, NULL, NULL, on_client_event, client);
  bufferevent_enable(client->bev, EV_READ | EV_WRITE);
  socket_set_nodelay(fd);
  server->clients_count++;
  printf("Accepted connection from %s:%d\n",
         inet_ntoa(((struct sockaddr_in *)addr)->sin_addr),
         ntohs(((struct sockaddr_in *)addr)->sin_port));
}


static struct connection *make_connection(struct server *server, int id,
                                  struct sockaddr_in *addr) {
  struct connection *p = malloc(sizeof(struct connection));
  p->id = id;
  p->addr = *addr;
  p->bev = bufferevent_socket_new(server->client->base, -1, BEV_OPT_CLOSE_ON_FREE);
  p->server = server;
  p->status = BEV_EVENT_EOF;
  return p;
}


int server_listen(struct server *p, char * ip, int port) {
  struct sockaddr_in addr;
  unsigned flags =
      LEV_OPT_CLOSE_ON_EXEC | LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE;

  /* listen on the given port at address 0.0.0.0 */
  memset(&addr, 0, sizeof(struct sockaddr_in));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(0);
  addr.sin_port = htons(port);
  p->listener = evconnlistener_new_bind(p->client->base, on_accept, p, flags, -1,
                                        (struct sockaddr *)&addr, sizeof(addr));
  if (p->listener == NULL) {
    printf("Failed to bind on port %d\n", port);
    exit(1);
  }
  evconnlistener_set_error_cb(p->listener, on_listener_error);
  printf("Listening on %s %d\n", ip, port);
  return 1;
}


static void on_connect(struct bufferevent *bev, short events, void *arg) {
  struct client *c = arg;

  if (events & BEV_EVENT_CONNECTED) {
    printf("Connected to server\n");
  } else {
    printf("ERROR: %s\n", evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
		// exit(1);
  }
}

struct bufferevent *connect_to_server(struct client *c, const char *ip, int port) {
  struct bufferevent *bev;
  int flag = 1;
  struct sockaddr_in addr = address_to_sockaddr(ip, port);

  bev = bufferevent_socket_new(c->base, -1, BEV_OPT_CLOSE_ON_FREE);
  bufferevent_setcb(bev, on_read_sock, NULL, on_connect, c);
  bufferevent_enable(bev, EV_READ | EV_WRITE);
  bufferevent_socket_connect(bev, (struct sockaddr *)&addr, sizeof(addr));
  setsockopt(bufferevent_getfd(bev), IPPROTO_TCP, TCP_NODELAY, &flag,
             sizeof(int));
  return bev;
}

void usage(const char* name){
	printf("Client Usage: %s [path/to/paxos.conf] [-h] [-o] [-v] [-p] [-c] [-l] [-d] [-s]\n", name);
	printf("  %-30s%s\n", "-h, --help", "Output this message and exit");
	printf("  %-30s%s\n", "-o, --outstanding #", "Number of outstanding client values");
	printf("  %-30s%s\n", "-v, --value-size #", "Size of client value (in bytes)");
	printf("  %-30s%s\n", "-p, --proposer-id #", "id of the proposer to connect to");
  printf("  %-30s%s\n", "-c, --client", "if this is a client (can send)");
	printf("  %-30s%s\n", "-l, --learner #", "if this is a learner (add also the trim value)");
	printf("  %-30s%s\n", "-d, --device #", "id of the klearner where to connect");
	printf("  %-30s%s\n", "-s, --socket # #", "ip and port of the learner to connect");
	exit(1);
}


static void free_connection(struct connection *p) {
  bufferevent_free(p->bev);
  free(p);
}


static void free_all_connections(struct connection **p, int count) {
  int i;
  for (i = 0; i < count; i++)
    free_connection(p[i]);
  if (count > 0)
    free(p);
}


static void server_free(struct server *p) {
  free_all_connections(p->connections, p->clients_count);
  if (p->listener != NULL)
    evconnlistener_free(p->listener);
  free(p);
}


void client_free(struct client* c, int chardevice, int send, int sock){
	event_free(c->sig);
	if(chardevice)
		event_free(c->evread);

	if(send){
		free(c->send_buffer);
		event_free(c->stats_ev);
		event_free(c->resend_ev);
		close(c->socket);
	}

	if(sock){
		if(send)
      bufferevent_free(c->bev);
		else
      server_free(c->s);
	}
	event_base_free(c->base);
	free(c);
}
