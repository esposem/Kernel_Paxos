#ifndef KERN_UDP
#define KERN_UDP

#include <asm/atomic.h>
#include <linux/udp.h>


struct udp_service
{
  // struct socket * u_socket;
  struct task_struct * u_thread;
  char * name;
  atomic_t thread_running;  //1 yes 0 no
  atomic_t socket_allocated;//1 yes 0 no
};

#define MAX_RCV_WAIT 100000 // in microseconds
#define MAX_UDP_SIZE 65507

#define VFC "VALUE FROM CLIENT"
#define P1A "PREPARE 1A"
#define P1B "PROMISE 1B"
#define A2A "ACCEPT REQ 2A"
#define A2B "ACCEPTED 2B"
#define AD "ALL DONE"

typedef struct udp_service udp_service;

extern u32 create_address(u8 *ip);
extern void prepare_sockaddr(struct sockaddr_in * address, int port, struct in_addr * addr, unsigned char * ip);
extern void udp_server_init(udp_service * k, struct socket ** s, struct sockaddr_in * address);
extern void init_service(udp_service * k, char * name);
extern void _send_message(struct socket * s, struct sockaddr_in * a, unsigned char * buff, int p, char * data, int len, char * module_name);
extern int udp_server_send(struct socket *sock, struct sockaddr_in * address, const char *buf, const size_t length, char * module_name);
extern int udp_server_receive(struct socket *sock, struct sockaddr_in *address, unsigned char *buf, unsigned long flags, udp_service * k);
extern void check_sock_allocation(udp_service * k, struct socket * s);
extern void udp_server_quit(udp_service * k, struct socket * s);

#endif
