#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <net/udp.h>
#include <asm/atomic.h>
#include <linux/time.h>
#include <net/sock.h>

#include "kernel_udp.h"

#define MODULE_NAME "Acceptor: "
#define MAX_RCV_WAIT 100000 // in microseconds
#define MAX_UDP_SIZE 65507

#define P1A "PREPARE 1A"
#define A2A "ACCEPT REQ 2A"
#define P1B "PROMISE 1B"
#define A2B "ACCEPTED 2B"

static unsigned char learnerip[5] = {127,0,0,4,'\0'};
static int learnerport = 3000;
static unsigned char myip[5] = {127,0,0,3,'\0'};
static int myport = 3003;
static struct in_addr proposeraddr;
static unsigned short proposerport;

module_param(myport, int, S_IRUGO);
MODULE_PARM_DESC(myport,"The receiving port, default 3003");

static int len = 50;
module_param(len, int, S_IRUGO);
MODULE_PARM_DESC(len,"Data packet length, default 50, max 65507 (automatically added space for terminating 0)");

static udp_service * kacceptor;
static atomic_t released_socket = ATOMIC_INIT(0); // 0 no, 1 yes
static atomic_t thread_running = ATOMIC_INIT(0);   // 0 no, 1 yes
static atomic_t struct_allocated = ATOMIC_INIT(0); // 0 no, 1 yes


int connection_handler(void *data)
{
  struct sockaddr_in address;
  struct socket *accept_socket = kacceptor->u_socket;
  int ret;

  unsigned char * in_buf = kmalloc(len, GFP_KERNEL);
  unsigned char * out_buf = kmalloc(len, GFP_KERNEL);
  size_t size_msg, size_msg1, size_buf;


  size_msg = strlen(P1A);
  size_msg1 = strlen(A2A);

  while (1){

    if(kthread_should_stop() || signal_pending(current)){
      // printk(KERN_INFO MODULE_NAME"STOP [connection_handler]");
      if(atomic_read(&released_socket) == 0){
        printk(KERN_INFO MODULE_NAME"Released socket [connection_handler]");
        atomic_set(&released_socket, 1);
        sock_release(kacceptor->u_socket);
      }
      kfree(in_buf);
      kfree(out_buf);
      return 0;
    }

    memset(in_buf, '\0', len);
    memset(&address, 0, sizeof(struct sockaddr_in));
    ret = udp_server_receive(accept_socket, &address, in_buf, len, MSG_WAITALL, &released_socket, MODULE_NAME);
    if(ret > 0){
      size_buf = strlen(in_buf);
      if(memcmp(in_buf, P1A, size_buf > size_msg ? size_msg : size_buf) == 0){
        memcpy(&proposeraddr, &address.sin_addr, sizeof(struct in_addr));
        unsigned short i = ntohs(address.sin_port);
        memcpy(&proposerport, &i, sizeof(unsigned short));
        // address is same as receiver
        _send_message(accept_socket, &address, out_buf, proposerport, P1B, len, MODULE_NAME);
      }else if (memcmp(in_buf, A2A, size_buf > size_msg1 ? size_msg1 : size_buf) == 0){
        // address is same as receiver
        _send_message(accept_socket, &address, out_buf, proposerport, A2B, len, MODULE_NAME);
        address.sin_addr.s_addr = htonl(create_address(learnerip));
        _send_message(accept_socket, &address, out_buf, learnerport, A2B, len, MODULE_NAME);
      }else{
        printk(KERN_INFO MODULE_NAME"Received %s?", in_buf);
      }
    }
  }

  return 0;
}

int udp_server_listen(void)
{
  int server_err;
  struct socket *conn_socket;
  struct sockaddr_in server;
  struct timeval tv;

  server_err = sock_create(PF_INET, SOCK_DGRAM, IPPROTO_UDP, &kacceptor->u_socket);
  if(server_err < 0){
    printk(KERN_INFO MODULE_NAME"Error: %d while creating socket [udp_server_listen]", server_err);
    atomic_set(&thread_running, 0);
    return 0;
  }else{
    atomic_set(&released_socket, 0);
    printk(KERN_INFO MODULE_NAME"Created socket [udp_server_listen]");
  }

  conn_socket = kacceptor->u_socket;
  server.sin_addr.s_addr = htonl(create_address(myip));
  server.sin_family = AF_INET;
  server.sin_port = htons(myport);

  server_err = conn_socket->ops->bind(conn_socket, (struct sockaddr*)&server, sizeof(server));
  if(server_err < 0) {
    printk(KERN_INFO MODULE_NAME"Error: %d while binding socket [udp_server_listen]", server_err);
    atomic_set(&released_socket, 1);
    sock_release(kacceptor->u_socket);
    atomic_set(&thread_running, 0);
    return 0;
  }else{
    printk(KERN_INFO MODULE_NAME"Socket is bind to 127.0.0.3 [udp_server_listen]");
  }

  tv.tv_sec = 0;
  tv.tv_usec = MAX_RCV_WAIT;
  kernel_setsockopt(conn_socket, SOL_SOCKET, SO_RCVTIMEO, (char * )&tv, sizeof(tv));

  connection_handler(NULL);
  atomic_set(&thread_running, 0);
  return 0;
}

void udp_server_start(void){
  kacceptor->u_thread = kthread_run((void *)udp_server_listen, NULL, MODULE_NAME);
  if(kacceptor->u_thread >= 0){
    atomic_set(&thread_running,1);
    printk(KERN_INFO MODULE_NAME "Thread running [udp_server_start]");
  }else{
    printk(KERN_INFO MODULE_NAME "Error in starting thread. Terminated [udp_server_start]");
  }
}

static int __init network_server_init(void)
{
  if(len < 0 || len > MAX_UDP_SIZE){
    printk(KERN_INFO MODULE_NAME"Wrong len, using default one");
    len = 50;
  }
  len++;
  atomic_set(&released_socket, 1);
  kacceptor = kmalloc(sizeof(udp_service), GFP_KERNEL);
  if(!kacceptor){
    printk(KERN_INFO MODULE_NAME"Failed to initialize server [network_server_init]");
  }else{
    atomic_set(&struct_allocated,1);
    memset(kacceptor, 0, sizeof(udp_service));
    printk(KERN_INFO MODULE_NAME "Server initialized [network_server_init]");
    udp_server_start();
  }
  return 0;
}

static void __exit network_server_exit(void)
{
  udp_server_quit(kacceptor, &struct_allocated, &thread_running, &released_socket, MODULE_NAME);
}


module_init(network_server_init)
module_exit(network_server_exit)
MODULE_LICENSE("MIT");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");
