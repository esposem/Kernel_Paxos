#ifndef USER_ETH
#define USER_ETH

#include "kernel_client.h"
#include "user_levent.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

extern void   eth_sendmsg(struct eth_connection* ethop,
                          uint8_t dest_addr[ETH_ALEN], void* clv, size_t size);
extern int    eth_init(struct eth_connection* ethop);
extern int    eth_listen(struct eth_connection* ethop);
extern size_t eth_recmsg(struct eth_connection* ethop,
                         uint8_t sndr_addr[ETH_ALEN], char* rmsg, size_t len);
extern int    str_to_mac(const char* str, uint8_t daddr[ETH_ALEN]);
extern int    mac_to_str(uint8_t daddr[ETH_ALEN], char* str);

#endif
