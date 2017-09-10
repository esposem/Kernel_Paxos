#ifndef USER_CLIENT
#define USER_CLIENT

#include "kernel_client.h"
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <errno.h>

#define BUFFER_LENGTH 8192

extern int learner_id;
extern struct client * cl;

struct client
{
	int id;
	int fd;
  int socket;
	int value_size;
	char* send_buffer;
	struct server * s;
	struct stats stats;
	struct event *evread;
	struct event* stats_ev;
	struct event* resend_ev;
	struct event_base* base;
	struct sockaddr_in si_other;
	struct timeval stats_interval;
	struct timeval reset_interval;
	struct bufferevent *bev;
	struct event* sig;
};

struct connection {
  int id;
  int status;
  struct bufferevent *bev;
  struct sockaddr_in addr;
  char *buffer;
  struct server *server;
};

struct server {
  int port;
  int clients_count;
  struct connection **clients; /* server we accepted connections from */
  struct evconnlistener *listener;
  struct event_base *base;
};

extern void open_file(struct client * c);
extern void usage(const char* name);
extern void handle_sigint(int sig, short ev, void* arg);
extern void client_submit_value(struct client* c);
extern void client_free(struct client* c, int chardevice, int send, int sock);
extern void write_file(int fd, void * data, size_t size);
extern int server_listen(struct server *p, char * ip, int port);
extern struct server *server_new(struct event_base *base);
extern void on_read_sock(struct bufferevent *bev, void *arg);
struct bufferevent *connect_to_server(struct client *c, const char *ip, int port);

#endif
