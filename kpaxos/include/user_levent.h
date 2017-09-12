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
	int value_size;
	struct event_base* base;
	struct event* sig;

	// Statistics
	struct stats stats;
	struct timeval stats_interval;
	struct event* stats_ev;

	// File op
	int fd;
	struct event *evread;

	// UDP op
  int socket;
	char* send_buffer;
	struct sockaddr_in prop_addr;
	struct timeval resend_interval;
	struct event* resend_ev;

	// TCP op
	struct server * s;
	struct bufferevent *bev;
};

struct connection {
  int id;
	int cl_id;
  int status;
  struct bufferevent *bev;
  struct sockaddr_in addr;
  struct server *server;
};

struct server {
  int clients_count;
  struct connection **connections; /* server we accepted connections from */
  struct evconnlistener *listener;
  struct client * client;
};

extern void open_file(struct client * c);
extern void usage(const char* name);
extern void handle_sigint(int sig, short ev, void* arg);
extern void client_submit_value(struct client* c);
extern void client_free(struct client* c, int chardevice, int send, int sock);
extern void write_file(int fd, void * data, int flag, size_t size);
extern int server_listen(struct server *p, char * ip, int port);
extern struct server *server_new(struct client *base);
extern void on_read_sock(struct bufferevent *bev, void *arg);
struct bufferevent *connect_to_server(struct client *c, const char *ip, int port);

#endif
