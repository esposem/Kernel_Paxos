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

int
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
    return 1;
  }
  return 0;
}

void
usage(const char* name, int client)
{
  printf("Client Usage: %s [options] \n", name);
  printf("Options:\n");
  printf("  %-30s%s\n", "-h, --help", "Output this message and exit");
  printf("  %-30s%s\n", "-c, --chardev_id #", "Chardevice id");
  printf("  %-30s%s\n", "-i, --if_name #", "Interface name (MAC)");
  if (client) {
    printf("  %-30s%s\n", "-l, --learner-addr #", "Learner address (MAC)");
    printf("  %-30s%s\n", "-p, --proposer-id #", "Proposer id");
    printf("  %-30s%s\n", "-f, --file #", "config file");
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
struct connection_list
{
  int                progr_id;
  struct connection* head;
  struct connection* current;
};

struct server*
server_new()
{
  struct server* p = malloc(sizeof(struct server));
  memset(p, 0, sizeof(struct server));
  p->ethop.socket = -1;
  p->fileop.fd = -1;
  return p;
}

static struct connection*
new_connection(int start, int end, int id, uint8_t address[ETH_ALEN])
{
  struct connection* conn = malloc(sizeof(struct connection));
  conn->start_id = start;
  conn->end_id = end;
  conn->next = NULL;
  conn->id = id;
  memcpy(conn->address, address, ETH_ALEN);
  return conn;
}

int
add_connection(struct server* serv, int start, int end,
               uint8_t address[ETH_ALEN])
{
  struct connection_list* clist = serv->connections;
  struct connection*      conn =
    new_connection(start, end, clist->progr_id, address);

  if (clist->head == NULL) {
    clist->head = conn;
    clist->current = conn;
  } else
    clist->current->next = conn;

  char arr[20];
  mac_to_str(address, arr);
  printf("Accepted connection from client %d %s", clist->progr_id, arr);

  return clist->progr_id++;
}

void
print_all_conn(struct server* serv)
{
  struct connection_list* clist = serv->connections;
  struct connection*      conn = clist->head;
  while (conn != NULL) {
    printf("%d -> \n", conn->id);
    conn = conn->next;
  }
}

struct connection*
find_connection(struct server* serv, int val)
{
  struct connection_list* clist = serv->connections;
  struct connection*      conn = clist->head;
  while (conn != NULL) {
    if (val >= conn->start_id && val < conn->end_id) {
      return conn;
    }
    conn = conn->next;
  }
  return NULL;
}

void
rem_connection(struct server* serv, int id)
{
  struct connection_list* clist = serv->connections;
  struct connection*      conn = clist->head;
  struct connection*      prev_conn = NULL;
  while (conn != NULL) {
    if (id == conn->id) {
      if (prev_conn != NULL)
        prev_conn->next = conn->next;
      if (conn == clist->head)
        clist->head = conn->next;
      if (conn == clist->current)
        clist->current = prev_conn;

      char arr[20];
      mac_to_str(conn->address, arr);
      printf("Closed connection with client %d %s", conn->id, arr);

      free(conn);
      return;
    }
    prev_conn = conn;
    conn = conn->next;
  }
}

static void
rem_all_connections(struct server* serv)
{
  struct connection_list* clist = serv->connections;
  struct connection*      conn = clist->head;
  while (conn != NULL) {
    struct connection* tmp = conn;
    free(tmp);
    conn = conn->next;
  }
}

void
new_connection_list(struct server* serv)
{
  serv->connections = malloc(sizeof(struct connection_list));
  serv->connections->head = NULL;
  serv->connections->current = NULL;
  serv->connections->progr_id = 0;
}

void
server_free(struct server* serv)
{
  rem_all_connections(serv);
  if (serv->fileop.fd >= 0)
    close(serv->fileop.fd);
  if (serv->ethop.socket >= 0)
    close(serv->ethop.socket);
  free(serv);
}

/* ################## Client ################ */

struct client*
client_new()
{
  struct client* cl = malloc(sizeof(struct client));
  memset(cl, 0, sizeof(struct client));
  cl->ethop.if_name = "enp4s0";
  cl->ethop.socket = -1;
  cl->fileop.fd = -1;
  cl->value_size = 64;
  cl->outstanding = 1;
  cl->nclients = 1;
  return cl;
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
prepare_clval(struct client* cl)
{
  cl->val = (struct client_value*)cl->send_buffer;
  cl->val->size = cl->value_size;
  random_string(cl->val->value, cl->value_size);
}

void
print_settings(struct client* cl)
{
  printf("if_name %s\n", cl->ethop.if_name);
  char a[20];
  mac_to_str(cl->prop_addr, a);
  printf("proposer address %s", a);
  mac_to_str(cl->learner_addr, a);
  printf("learner address %s", a);
  printf("id: %d ------ nclients: %d\n", cl->id, cl->nclients);
}

void
client_free(struct client* cl)
{
  if (cl->ethop.socket >= 0)
    close(cl->ethop.socket);

  if (cl->fileop.fd >= 0)
    close(cl->fileop.fd);
  free(cl->nclients_time);
  free(cl);
}

void
client_submit_value(struct client* cl, int id)
{
  struct client_value* val = cl->val;
  val->client_id = id;
  gettimeofday(&cl->nclients_time[id - cl->id], NULL);
  val->t = cl->nclients_time[id - cl->id];
  eth_sendmsg(&cl->ethop, cl->prop_addr, val, cl->send_buffer_len);
  // printf("Client %d submitted value %.16s with %zu bytes\n", v->client_id,
  //        v->value, v->size);
}

int
find_proposer(struct client* cl, char* path)
{
  FILE* f;
  int   id;
  char  addr[20];
  char  line[512];
  char* l;

  if ((f = fopen(path, "r")) == NULL) {
    perror("fopen");
  }
  while (fgets(line, sizeof(line), f) != NULL) {
    if (line[0] != '#' && line[0] != '\n') {
      l = line;
      while (*l == ' ') {
        l++;
      }
      if (sscanf(l, "proposer %d %s", &id, addr) == 2 && id == cl->prop_id) {
        str_to_mac(addr, cl->prop_addr);
        return 0;
      }
    }
  }
  return 1;
}
