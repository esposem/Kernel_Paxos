#ifndef USER_CLIENT
#define USER_CLIENT

#include "kernel_client.h"
#include <arpa/inet.h>
#include <errno.h>
#include <net/ethernet.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <unistd.h>

enum user_communication
{
  OPEN_CONN = 100,
  OK,
  CLOSE_CONN
};

// Common structs
struct chardevice
{
  int fd;
  int char_device_id;
};

struct eth_connection
{
  int   socket;
  char  rec_buffer[ETH_DATA_LEN];
  char* if_name;
};

// Server side

struct connection
{
  int                id;
  int                start_id;
  int                end_id;
  uint8_t            address[ETH_ALEN];
  struct connection* next;
};

struct server
{
  struct connection_list* connections; // client we accepted connections from
  struct chardevice       fileop;      // File op
  struct eth_connection   ethop;       // ETH op
};

// client side
struct client
{
  int                   id;
  int                   value_size;
  int                   outstanding;
  int                   nclients;
  struct timeval*       nclients_time;
  struct stats          stats; /* Statistics */
  struct timeval        stats_interval;
  struct client_value*  val; // refers to the send_buffer
  char                  send_buffer[ETH_DATA_LEN];
  int                   send_buffer_len;
  int                   learner_id;
  struct chardevice     fileop; // File op
  struct eth_connection ethop;  // ETH op
  uint8_t               prop_addr[ETH_ALEN];
  uint8_t               learner_addr[ETH_ALEN];
};

extern int  open_file(struct chardevice* c);
extern void write_file(int fd, void* data, size_t size);
extern void usage(const char* name, int client);

extern struct server* server_new();
extern int            add_connection(struct server* serv, int start, int end,
                                     uint8_t address[ETH_ALEN]);
extern struct connection* find_connection(struct server* serv, int val);
extern void               rem_connection(struct server* serv, int id);
extern void               new_connection_list(struct server* serv);
extern void               print_all_conn(struct server* serv);
extern void               server_free(struct server* c);
extern struct server*     server_new();

extern struct client* client_new();
extern void           prepare_clval(struct client* cl);
extern void           print_settings(struct client* cl);
extern void           client_free(struct client* cl);
extern void           client_submit_value(struct client* cl, int id);

#endif
