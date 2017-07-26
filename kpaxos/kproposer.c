#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <net/udp.h>
#include <asm/atomic.h>
#include <linux/time.h>
#include <net/sock.h>

#include "kernel_udp.h"

#define VFC "VALUE FROM CLIENT"
#define P1B "PROMISE 1B"
#define A2B "ACCEPTED 2B"
#define P1A "PREPARE 1A"
#define A2A "ACCEPT REQ 2A"
#define AD "ALL DONE"

#define MODULE_NAME "Proposer: "
#define MAX_RCV_WAIT 100000 // in microseconds
#define MAX_UDP_SIZE 65507

static unsigned char acceptorip[5] = {127,0,0,3,'\0'};
static int acceptorport = 3003;
static unsigned char myip[5] = {127,0,0,2,'\0'};
static int myport = 3002;
static struct in_addr clientaddr;
static unsigned short clientport;

module_param(myport, int, S_IRUGO);
MODULE_PARM_DESC(myport,"The receiving port, default 3002");

static int len = 50;
module_param(len, int, S_IRUGO);
MODULE_PARM_DESC(len,"Data packet length, default 50, max 65507 (automatically added space for terminating 0)");


static udp_service * kproposer;
static atomic_t released_socket = ATOMIC_INIT(0); // 0 no, 1 yes
static atomic_t thread_running = ATOMIC_INIT(0);   // 0 no, 1 yes
static atomic_t struct_allocated = ATOMIC_INIT(0); // 0 no, 1 yes



int connection_handler(void *data)
{
  struct sockaddr_in address;
  struct socket *proposer_socket = kproposer->u_socket;

  int ret;
  unsigned char * in_buf = kmalloc(len, GFP_KERNEL);
  unsigned char * out_buf = kmalloc(len, GFP_KERNEL);
  size_t size_buf, size_msg1, size_msg2, size_msg3;


  size_msg1 = strlen(VFC);
  size_msg2 = strlen(P1B);
  size_msg3 = strlen(A2B);

  while (1){

    if(kthread_should_stop() || signal_pending(current)){
      // printk(KERN_INFO MODULE_NAME"STOP [connection_handler]");
      if(atomic_read(&released_socket) == 0){
        printk(KERN_INFO MODULE_NAME"Released socket [connection_handler]");
        atomic_set(&released_socket, 1);
        sock_release(kproposer->u_socket);
      }
      kfree(in_buf);
      kfree(out_buf);
      return 0;
    }

    memset(in_buf, '\0', len);
    memset(&address, 0, sizeof(struct sockaddr_in));
    ret = udp_server_receive(proposer_socket, &address, in_buf, len, MSG_WAITALL, &released_socket, MODULE_NAME);
    if(ret > 0){
      size_buf = strlen(in_buf);
      if(memcmp(in_buf, VFC, size_buf > size_msg1 ? size_msg1 : size_buf) == 0){
        memcpy(&clientaddr, &address.sin_addr, sizeof(struct in_addr));
        unsigned short i = ntohs(address.sin_port);
        memcpy(&clientport, &i, sizeof(unsigned short));
        address.sin_addr.s_addr = htonl(create_address(acceptorip));
        _send_message(proposer_socket, &address, out_buf, acceptorport, P1A, len, MODULE_NAME);

      }else if (memcmp(in_buf, P1B, size_buf > size_msg2 ? size_msg2 : size_buf) == 0){
        // address.sin_addr.s_addr = htonl(create_address(acceptorip));
        _send_message(proposer_socket, &address, out_buf, acceptorport, A2A, len, MODULE_NAME);

      } else if (memcmp(in_buf, A2B, size_buf > size_msg3 ? size_msg3 : size_buf) == 0){
        address.sin_addr = clientaddr;
        // TODO retrieve client port
        _send_message(proposer_socket, &address, out_buf, 3001, "ALL DONE", len, MODULE_NAME);

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

  server_err = sock_create(PF_INET, SOCK_DGRAM, IPPROTO_UDP, &kproposer->u_socket);
  if(server_err < 0){
    printk(KERN_INFO MODULE_NAME"Error: %d while creating socket [udp_server_listen]", server_err);
    atomic_set(&thread_running, 0);
    return 0;
  }else{
    atomic_set(&released_socket, 0);
    printk(KERN_INFO MODULE_NAME"Created socket [udp_server_listen]");
  }

  conn_socket = kproposer->u_socket;
  server.sin_addr.s_addr = htonl(create_address(myip));
  server.sin_family = AF_INET;
  server.sin_port = htons(myport);

  server_err = conn_socket->ops->bind(conn_socket, (struct sockaddr*)&server, sizeof(server));
  if(server_err < 0) {
    printk(KERN_INFO MODULE_NAME"Error: %d while binding socket [udp_server_listen]", server_err);
    atomic_set(&released_socket, 1);
    sock_release(kproposer->u_socket);
    atomic_set(&thread_running, 0);
    return 0;
  }else{
    printk(KERN_INFO MODULE_NAME"Socket is bind to 127.0.0.2 [udp_server_listen]");
  }

  tv.tv_sec = 0;
  tv.tv_usec = MAX_RCV_WAIT;
  kernel_setsockopt(conn_socket, SOL_SOCKET, SO_RCVTIMEO, (char * )&tv, sizeof(tv));

  connection_handler(NULL);
  atomic_set(&thread_running, 0);
  return 0;
}

void udp_server_start(void){
  kproposer->u_thread = kthread_run((void *)udp_server_listen, NULL, MODULE_NAME);
  if(kproposer->u_thread >= 0){
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
  kproposer = kmalloc(sizeof(udp_service), GFP_KERNEL);
  if(!kproposer){
    printk(KERN_INFO MODULE_NAME"Failed to initialize server [network_server_init]");
  }else{
    atomic_set(&struct_allocated,1);
    memset(kproposer, 0, sizeof(udp_service));
    printk(KERN_INFO MODULE_NAME "Server initialized [network_server_init]");
    udp_server_start();
  }
  return 0;
}

static void __exit network_server_exit(void)
{
  udp_server_quit(kproposer, &struct_allocated, &thread_running, &released_socket, MODULE_NAME);
}

module_init(network_server_init)
module_exit(network_server_exit)
MODULE_LICENSE("MIT");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");
