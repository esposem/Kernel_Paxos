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

int
str_to_mac(const char* str, uint8_t daddr[ETH_ALEN])
{
  int values[6], i;
  if (6 == sscanf(str, "%x:%x:%x:%x:%x:%x", &values[0], &values[1], &values[2],
                  &values[3], &values[4], &values[5])) {
    /* convert to uint8_t */
    for (i = 0; i < 6; ++i)
      daddr[i] = (uint8_t)values[i];
    return 1;
  }
  return 0;
}

int
mac_to_str(uint8_t daddr[ETH_ALEN], char* str)
{
  sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x\n", daddr[0], daddr[1], daddr[2],
          daddr[3], daddr[4], daddr[5]);
  return 1;
}

size_t
eth_recmsg(struct eth_connection* ethop, uint8_t sndr_addr[ETH_ALEN],
           char* rmsg, size_t len)
{
  char                 buf[ETH_FRAME_LEN] = { 0 };
  struct ether_header* eh = (struct ether_header*)buf;
  ssize_t              received;

  received = recvfrom(ethop->socket, buf, ETH_FRAME_LEN, 0, NULL, NULL);
  if (received <= 0) {
    perror("recvfrom()");
    return 0;
  }

  if (sndr_addr)
    memcpy(sndr_addr, eh->ether_shost, ETH_ALEN);

  received -= sizeof(*eh);
  received -= sizeof(PAXOS_CLIENT_VALUE);

  // if message too big than given buffer, cut it
  if (received > len)
    received = len;

  memcpy(rmsg, buf + sizeof(*eh) + sizeof(PAXOS_CLIENT_VALUE), received);
  return received;
}

void
eth_sendmsg(struct eth_connection* ethop, uint8_t dest_addr[ETH_ALEN],
            void* clv, size_t size)
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
  memcpy(eh->ether_dhost, dest_addr, ETH_ALEN);
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
  memcpy(sock_addr.sll_addr, dest_addr, ETH_ALEN);

  if (sendto(ethop->socket, buf, send_len, 0, (struct sockaddr*)&sock_addr,
             sizeof(sock_addr)) < 0) {
    perror("sendto()");
  } else {
    char arr[20];
    mac_to_str(dest_addr, arr);
    // printf("Sent to %s", arr);
  }
}

int
eth_listen(struct eth_connection* ethop)
{
  struct ifreq ifr;
  int          s;

  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, ethop->if_name, IFNAMSIZ - 1);

  /* Set interface to promiscuous mode. */
  if (ioctl(ethop->socket, SIOCGIFFLAGS, &ifr) < 0) {
    perror("Error setting the SIOCGIFFLAGS");
    close(ethop->socket);
    return 1;
  }
  ifr.ifr_flags |= IFF_PROMISC;
  if (ioctl(ethop->socket, SIOCSIFFLAGS, &ifr) < 0) {
    perror("Error setting the SIOCSIFFLAGS");
    close(ethop->socket);
    return 1;
  }

  /* Allow the socket to be reused. */
  s = 1;
  if (setsockopt(ethop->socket, SOL_SOCKET, SO_REUSEADDR, &s, sizeof(s)) < 0) {
    perror("Error setting the socket as reused");
    close(ethop->socket);
    return 1;
  }

  /* Bind to device. */
  if (setsockopt(ethop->socket, SOL_SOCKET, SO_BINDTODEVICE, ethop->if_name,
                 IFNAMSIZ - 1) < 0) {
    perror("Error binding the device");
    close(ethop->socket);
    return 1;
  }

  return 0;
}

int
eth_init(struct eth_connection* ethop)
{

  struct ifreq ifr;
  ethop->socket = socket(AF_PACKET, SOCK_RAW, htons(PAXOS_CLIENT_VALUE));

  if (ethop->socket < 0) {
    printf("Socket not working, maybe you are not using sudo?\n");
    return 1;
  }

  /* Get the index number and MAC address of ethernet interface. */
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, ethop->if_name, IFNAMSIZ - 1);
  if (ioctl(ethop->socket, SIOCGIFINDEX, &ifr) < 0) {
    perror("Error getting the interface index");
    return 1;
  }
  if_index = ifr.ifr_ifindex;
  if (ioctl(ethop->socket, SIOCGIFHWADDR, &ifr) < 0) {
    perror("Error getting the interface MAC address");
    return 1;
  }
  memcpy(if_addr, ifr.ifr_hwaddr.sa_data, ETH_ALEN);
  return 0;
}
