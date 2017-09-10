#ifndef USER_UDP
#define USER_UDP

#include "kernel_client.h"
#include "user_levent.h"

extern void udp_send_msg(struct client_value * clv, size_t size);
extern void init_socket(struct client * c);
extern struct sockaddr_in address_to_sockaddr(const char *ip, int port);


#endif
