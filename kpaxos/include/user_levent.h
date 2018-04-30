#ifndef USER_CLIENT
#define USER_CLIENT

#include "kernel_client.h"
#include <arpa/inet.h>
#include <errno.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <net/ethernet.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <unistd.h>

struct chardevice
{
  int           fd;
  int           char_device_id;
  struct event* evread;
};

struct eth_connection
{
  int            socket;
  char*          send_buffer;
  char*          if_name;
  uint8_t        prop_addr[ETH_ALEN];
  struct timeval resend_interval;
  struct event*  resend_ev;
};

struct tcp_connection
{
  struct server*      s;
  char*               dest_addr;
  int                 dest_port;
  struct bufferevent* bev;
  char                rec_buffer[ETH_DATA_LEN];
};

struct client
{
  int                id;
  int                value_size;
  struct event_base* base;
  struct event*      sig;
  int                outstanding;

  // Statistics
  struct stats   stats;
  struct timeval stats_interval;
  struct event*  stats_ev;
  // File op
  struct chardevice fileop;
  // UDP op
  struct eth_connection ethop;
  // TCP op
  struct tcp_connection tcpop;
};

struct connection
{
  int                 id;
  int                 cl_id;
  int                 status;
  struct bufferevent* bev;
  struct sockaddr_in  addr;
  struct server*      server;
};

struct server
{
  int                    clients_count;
  struct connection**    connections; /* server we accepted connections from */
  struct evconnlistener* listener;
  struct client*         client;
};

extern void open_file(struct client* cl);
extern void usage(const char* name, int client);
extern void handle_sigint(int sig, short ev, void* arg);
extern void client_submit_value(struct client* c);
extern void client_free(struct client* c, int chardevice, int send, int sock);
extern void write_file(int fd, void* data, size_t size);
extern int  server_listen(struct tcp_connection* tcp);
extern struct server* server_new(struct client* base);
extern void           on_read_sock(struct bufferevent* bev, void* arg);
struct bufferevent*   connect_to_server(struct client* c);

#endif
