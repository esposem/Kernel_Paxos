#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "paxos_types.h"
#include "user_eth.h"

static uint8_t if_addr[ETH_ALEN];
static int     if_index;

static void
serialize_int_to_big(uint32_t n, unsigned char** buffer)
{
  unsigned char* res = (unsigned char*)&n;
  (*buffer)[0] = res[3];
  (*buffer)[1] = res[2];
  (*buffer)[2] = res[1];
  (*buffer)[3] = res[0];
  *buffer += sizeof(uint32_t);
}

static void
cp_int_packet(uint32_t n, unsigned char** buffer)
{
  memcpy(*buffer, &n, sizeof(uint32_t));
  *buffer += sizeof(uint32_t);
}

// returns 1 if architecture is little endian, 0 in case of big endian.
static int
check_for_endianness()
{
  unsigned int x = 1;
  char*        c = (char*)&x;
  return (int)*c;
}

void
eth_sendmsg(struct client* cl, struct client_value* clv, size_t size)
{
  uint32_t             len = size;
  unsigned char*       tmp;
  char*                value = (char*)clv;
  unsigned char        buf[ETH_FRAME_LEN] = { 0 };
  size_t               send_len;
  struct ether_header* eh;
  struct sockaddr_ll   sock_addr;

  /* Construct ethernet header. */
  eh = (struct ether_header*)buf;
  memcpy(eh->ether_shost, if_addr, ETH_ALEN);
  memcpy(eh->ether_dhost, cl->ethop.prop_addr, ETH_ALEN);
  eh->ether_type = htons(PAXOS_CLIENT_VALUE);
  send_len = sizeof(*eh);

  /* Fill the packet data. */
  if (len + send_len >= ETH_FRAME_LEN)
    len = ETH_FRAME_LEN - send_len - sizeof(unsigned int);

  tmp = &(buf[send_len]);
  if (check_for_endianness()) {
    // Machine is little endian, transform the packet data from little
    // to big endian
    serialize_int_to_big(len, &tmp);
  } else {
    cp_int_packet(len, &tmp);
  }
  memcpy(tmp, value, len);
  send_len += len;
  send_len += (sizeof(unsigned int));

  /* Fill the destination address and send it. */
  sock_addr.sll_ifindex = if_index;
  sock_addr.sll_halen = ETH_ALEN;
  memcpy(sock_addr.sll_addr, cl->ethop.prop_addr, ETH_ALEN);

  if (sendto(cl->ethop.socket, buf, send_len, 0, (struct sockaddr*)&sock_addr,
             sizeof(sock_addr)) < 0) {
    perror("sendto()");
  }
}

int
eth_init(struct client* c)
{

  struct ifreq ifr;
  c->ethop.socket = socket(AF_PACKET, SOCK_RAW, IPPROTO_RAW);

  if (c->ethop.socket < 0) {
    printf("Socket not working, maybe you are not using sudo?\n");
    return 1;
  }

  /* Get the index number and MAC address of ethernet interface. */
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, c->ethop.if_name, IFNAMSIZ - 1);
  if (ioctl(c->ethop.socket, SIOCGIFINDEX, &ifr) < 0) {
    perror("Error getting the interface index");
    return 1;
  }
  if_index = ifr.ifr_ifindex;
  if (ioctl(c->ethop.socket, SIOCGIFHWADDR, &ifr) < 0) {
    perror("Error getting the interface MAC address");
    return 1;
  }
  memcpy(if_addr, ifr.ifr_hwaddr.sa_data, ETH_ALEN);
  printf("Socket ready\n");
  return 0;
}
