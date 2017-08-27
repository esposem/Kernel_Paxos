#ifndef KERN_UDP
#define KERN_UDP

#include <asm/atomic.h>
#include <linux/udp.h>
#include <linux/time.h>


#define MAX_RCV_WAIT 100000 // in microseconds (100 ms)
#define MAX_UDP_SIZE 65507

#define N_TIMER 3
#define PROP_TIM 0
#define ACC_TIM 1
#define LEA_TIM 2

extern struct timeval sk_timeout_timeval;

struct udp_service
{
  struct task_struct * u_thread;
  char * name;
  // 1 yes 0 no
  atomic_t thread_running;
  atomic_t socket_allocated;
  // 0 proposer   1 acceptor    2 learner
  void (*timer_cb[N_TIMER])(unsigned long);
  unsigned long data[N_TIMER];
  unsigned long timeout_jiffies[N_TIMER];
};

typedef struct udp_service udp_service;

extern u32 create_address(u8 *ip);
extern void prepare_sockaddr(struct sockaddr_in * address, int port, struct in_addr * addr, unsigned char * ip);
extern int udp_server_init(udp_service * k, struct socket ** s, struct sockaddr_in * address, atomic_t * allocated);
extern void init_service(udp_service * k, char * name, int id);
extern void _send_message(struct socket * s, struct sockaddr_in * a, unsigned char * buff, int p, char * data, int len, char * module_name);
extern int udp_server_send(struct socket *sock, struct sockaddr_in * address, const char *buf, const size_t length, char * module_name);
extern int udp_server_receive(struct socket *sock, struct sockaddr_in *address, unsigned char *buf, unsigned long flags, udp_service * k);
extern void check_sock_allocation(udp_service * k, struct socket * s, atomic_t * allocated);
extern void udp_server_quit(udp_service * k);

#endif
