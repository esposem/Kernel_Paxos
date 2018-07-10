#include "user_eth.h"
#include "user_levent.h"
#include "user_stats.h"
#include <getopt.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int stop = 1;

void
stop_execution(int signo)
{
  stop = 0;
}

static void
unpack_message(struct server* serv, size_t len)
{
  struct user_msg*     mess = (struct user_msg*)serv->ethop.rec_buffer;
  struct client_value* val = (struct client_value*)mess->value;
  struct connection*   conn = find_connection(serv, val->client_id);
  if (conn)
    eth_sendmsg(&serv->ethop, conn->address, mess, len);
}

static void
read_file(struct server* serv)
{
  int len = read(serv->fileop.fd, serv->ethop.rec_buffer, ETH_DATA_LEN);

  if (len < 0)
    return;

  if (len == 0) {
    printf("Stopped by kernel module\n");
    stop = 0;
  }
  unpack_message(serv, len);
}

static void
change_conn_status(struct server* serv, char* mess, uint8_t dest_addr[ETH_ALEN])
{
  int* buff = (int*)mess;
  if (buff[0] == OPEN_CONN) {
    int id = add_connection(serv, buff[1], buff[2], dest_addr);
    int buffs[2];
    buffs[0] = OK;
    buffs[1] = id;
    eth_sendmsg(&serv->ethop, dest_addr, buffs, sizeof(buffs));
  }
  if (buff[0] == CLOSE_CONN) {
    rem_connection(serv, buff[1]);
  }
}

void
read_socket(struct server* serv)
{
  ssize_t len;
  uint8_t src_addr[ETH_ALEN];
  len =
    eth_recmsg(&serv->ethop, src_addr, serv->ethop.rec_buffer, ETH_DATA_LEN);
  if (len > 0)
    change_conn_status(serv, serv->ethop.rec_buffer, src_addr);
}

static void
make_learner(struct server* serv)
{
  struct pollfd pol[2]; // 2 events: socket and file

  // chardevice
  if (open_file(&serv->fileop))
    goto cleanup;

  // socket
  if (eth_init(&serv->ethop))
    goto cleanup;

  if (eth_listen(&serv->ethop))
    goto cleanup;

  pol[0].fd = serv->ethop.socket;
  pol[0].events = POLLIN;
  pol[1].fd = serv->fileop.fd;
  pol[1].events = POLLIN;

  while (stop) {
    poll(pol, 2, -1);
    // send delivered values via socket
    if (pol[0].revents & POLLIN) {
      read_socket(serv);
    } else if (pol[1].revents & POLLIN) { // communicate to chardevice via file
      read_file(serv);
    }
  }

cleanup:
  server_free(serv);
}

static void
check_args(int argc, char* argv[], struct server* serv)
{
  int opt = 0, idx = 0;

  static struct option options[] = { { "chardev_id", required_argument, 0,
                                       'c' },
                                     { "if_name", required_argument, 0, 'i' },
                                     { "help", no_argument, 0, 'h' },
                                     { 0, 0, 0, 0 } };

  while ((opt = getopt_long(argc, argv, "c:i:h", options, &idx)) != -1) {
    switch (opt) {
      case 'c':
        serv->fileop.char_device_id = atoi(optarg);
        break;
      case 'i':
        serv->ethop.if_name = optarg;
        break;
      default:
        usage(argv[0], 0);
    }
  }
}

int
main(int argc, char* argv[])
{
  struct server* serv = server_new();
  serv->ethop.if_name = "enp0s3";
  serv->fileop.char_device_id = 0;
  new_connection_list(serv);

  check_args(argc, argv, serv);

  printf("if_name %s\n", serv->ethop.if_name);
  printf("chardevice /dev/paxos/klearner%c\n",
         serv->fileop.char_device_id + '0');
  signal(SIGINT, stop_execution);
  make_learner(serv);

  return 0;
}
