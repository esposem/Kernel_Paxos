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

// Common structs
struct chardevice
{
  int           fd;
  int           char_device_id;
  struct event* evread;
};

struct tcp_connection
{
  char*               dest_addr;
  int                 dest_port;
  struct bufferevent* bev;
  char                rec_buffer[ETH_DATA_LEN];
};

// Server side
struct connection
{
  int                 id;
  int                 start_id;
  int                 end_id;
  int                 status;
  struct bufferevent* bev;
  struct sockaddr_in  addr;
  struct server*      server;
};

struct server
{
  struct event_base* base;
  // struct event*          sig;
  int                    clients_count;
  struct connection**    connections; /* server we accepted connections from */
  struct evconnlistener* listener;
  // struct client*         client;
  struct chardevice     fileop; // File op
  struct tcp_connection tcpop;  // TCP op
};

// client side
struct eth_connection
{
  int                  socket;
  struct client_value* val; // refers to the send_buffer
  char                 send_buffer[ETH_DATA_LEN];
  int                  send_buffer_len;
  char*                if_name;
  uint8_t              prop_addr[ETH_ALEN];
};

struct client
{
  int                   id;
  int                   value_size;
  int                   outstanding;
  struct event_base*    base;
  struct event*         sig;
  int                   nclients;
  struct timeval*       nclients_time;
  struct stats          stats; /* Statistics */
  struct timeval        stats_interval;
  struct event*         stats_ev;
  struct chardevice     fileop; // File op
  struct eth_connection ethop;  // UDP op
  struct tcp_connection tcpop;  // TCP op
};

extern int  open_file(struct chardevice* c);
extern void write_file(int fd, void* data, size_t size);
extern void usage(const char* name, int client);
extern void on_read_sock(struct bufferevent* bev, void* arg);

extern void         handle_sigint(int sig, short ev, void* arg);
extern void         client_submit_value(struct client* c, int id);
extern void         client_free(struct client* c, int chardevice, int sock);
struct bufferevent* connect_to_server(struct client* c);

extern int  server_listen(struct server* serv);
extern void server_free(struct server* c);

extern struct server* server_new();

#endif
