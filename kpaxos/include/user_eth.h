#ifndef USER_ETH
#define USER_ETH

#include "kernel_client.h"
#include "user_levent.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

extern void eth_sendmsg(struct client* cl, struct client_value* clv,
                        size_t size);
extern void eth_init(struct client* c);

#endif
