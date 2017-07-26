#ifndef KERN_UDP
#define KERN_UDP

struct udp_service
{
  struct socket * u_socket;
  struct task_struct * u_thread;
};

typedef struct udp_service udp_service;

extern void _send_message(struct socket * s, struct sockaddr_in * a, unsigned char * buff, int p, char * data, int len, char * MODULE_NAME);
extern int udp_server_send(struct socket *sock, struct sockaddr_in *address, const char *buf, const size_t length, unsigned long flags, char * MODULE_NAME);
extern int udp_server_receive(struct socket *sock, struct sockaddr_in *address, unsigned char *buf,int size, unsigned long flags, \
                      atomic_t * released_socket, char * MODULE_NAME);
extern void udp_server_quit(udp_service * k, atomic_t * struct_allocated, atomic_t * thread_running,\
                    atomic_t * released_socket, char * MODULE_NAME);
extern u32 create_address(u8 *ip);

#endif
