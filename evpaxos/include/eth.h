#ifndef __ETH_H__
#define __ETH_H__

#include "peers.h"
#include <linux/if_ether.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/types.h>

// typedef void (*rcv_cb)(struct net_device* dev, uint8_t src_addr[ETH_ALEN],
//                        char* rmsg, size_t len);

struct net_device* eth_init(const char* if_name);
int eth_listen(struct net_device* dev, uint16_t proto, peer_cb cb, void* arg);
int eth_send(struct net_device* dev, uint8_t dest_addr[ETH_ALEN],
             uint16_t proto, const char* msg, size_t len);
int eth_destroy(struct net_device* dev);

int str_to_mac(const char* str, uint8_t daddr[ETH_ALEN]);
int mac_to_str(uint8_t daddr[ETH_ALEN], char* str);

#endif
