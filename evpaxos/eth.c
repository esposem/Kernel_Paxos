#include "eth.h"
#include "message.h"
#include <linux/if_ether.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <net/dst.h>

#define MAX_CALLBACK 3

struct callback
{
  struct packet_type pt;
  int                cb_index;
  peer_cb            cb[MAX_CALLBACK];
  void*              arg[MAX_CALLBACK];
};

static struct callback cbs[N_PAXOS_TYPES];
static int             if_lo = 0;

static int packet_recv(struct sk_buff* sk, struct net_device* dev,
                       struct packet_type* pt, struct net_device* dev2);

struct net_device*
eth_init(const char* if_name)
{
  memset(cbs, 0, sizeof(cbs));
  if (memcmp(if_name, "lo", 3) == 0)
    if_lo = 1;
  return dev_get_by_name(&init_net, if_name);
}

int
eth_subscribe(struct net_device* dev, uint16_t proto, peer_cb cb, void* arg)
{
  int i = GET_PAXOS_INDEX(proto); // cbs[i]

  if (i < 0 || i >= N_PAXOS_TYPES) {
    paxos_log_error("Wrong protocol!");
    return 0;
  }

  if (cbs[i].cb_index >= MAX_CALLBACK) {
    paxos_log_error("Callback full!");
    return 0;
  }

  for (int k = 0; k < cbs[i].cb_index; k++) {
    if (cbs[i].cb[k] == cb) {
      paxos_log_error("Callback already present!");
      return 0;
    }
  }

  cbs[i].cb[cbs[i].cb_index] = cb;
  cbs[i].arg[cbs[i].cb_index++] = arg;
  return 1;
}

int
eth_listen(struct net_device* dev)
{
  for (int i = 0; i < N_PAXOS_TYPES; i++) {
    if (cbs[i].cb_index > 0) {
      cbs[i].pt.type = htons(GET_PAXOS(i));
      cbs[i].pt.func = packet_recv;
      cbs[i].pt.dev = dev;
      paxos_log_debug("Added callback for proto %d", i);
      dev_add_pack(&cbs[i].pt);
    }
  }
  return 1;
}

static int
packet_recv(struct sk_buff* skb, struct net_device* dev, struct packet_type* pt,
            struct net_device* src_dev)
{
  uint16_t       proto = ntohs(skb->protocol);
  int            i = GET_PAXOS_INDEX(proto);
  paxos_message  msg;
  char           msg_data[ETH_DATA_LEN];
  struct ethhdr* eth = eth_hdr(skb);
  size_t         len = skb->len;
  char           data[ETH_DATA_LEN];
  char*          data_p = data;

  skb_copy_bits(skb, 0, data, len);

  if (!if_lo && memcmp(eth->h_source, dev->dev_addr, ETH_ALEN) == 0) {
    data_p += ETH_HLEN;
    len -= ETH_HLEN;
  }

  recv_paxos_message(&msg, msg_data, proto, data_p, len);
  for (int k = 0; k < cbs[i].cb_index; k++) {
    cbs[i].cb[k](&msg, cbs[i].arg[k], eth->h_source);
  }

  kfree_skb(skb);
  return 0;
}

// reimplemented dev_loopback_xmit without the warning
void
dev_loopback_xmit2(struct sk_buff* skb)
{
  skb_reset_mac_header(skb);
  __skb_pull(skb, skb_network_offset(skb));
  skb->pkt_type = PACKET_LOOPBACK;
  skb->ip_summed = CHECKSUM_UNNECESSARY;
  skb_dst_force(skb);
  netif_rx_ni(skb);
}

int
eth_send(struct net_device* dev, uint8_t dest_addr[ETH_ALEN], uint16_t proto,
         const char* msg, size_t len)
{
  int            ret = 0;
  unsigned char* data;

  struct sk_buff* skb = alloc_skb(ETH_FRAME_LEN, GFP_ATOMIC);

  skb->dev = dev;
  skb->pkt_type = PACKET_OUTGOING;

  skb_reserve(skb, ETH_HLEN);
  /*changing Mac address */
  struct ethhdr* eth = (struct ethhdr*)skb_push(skb, ETH_HLEN);
  skb->protocol = eth->h_proto = htons(proto);
  memcpy(eth->h_source, dev->dev_addr, ETH_ALEN);
  memcpy(eth->h_dest, dest_addr, ETH_ALEN);

  /* put the data and send the packet */
  if (len > ETH_DATA_LEN)
    len = ETH_DATA_LEN;

  data = skb_put(skb, len);
  memcpy(data, msg, len);
  if (memcmp(dest_addr, dev->dev_addr, ETH_ALEN) == 0) {
    dev_loopback_xmit2(skb);
  } else {
    ret = dev_queue_xmit(skb);
  }
  return !ret;
}

int
eth_destroy(struct net_device* dev)
{
  int i;

  for (i = 0; i < N_PAXOS_TYPES; ++i) {
    if (cbs[i].cb_index > 0)
      dev_remove_pack(&cbs[i].pt);
  }

  return 1;
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
