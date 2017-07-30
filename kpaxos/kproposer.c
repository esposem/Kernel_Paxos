#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <net/udp.h>
#include <asm/atomic.h>
#include <linux/time.h>
#include <net/sock.h>

#include "kernel_udp.h"

static unsigned char acceptorip[5] = {127,0,0,3,'\0'};
static int acceptorport = 3003;
static unsigned char myip[5] = {127,0,0,2,'\0'};
static int myport = 3002;
static struct in_addr clientaddr;
static unsigned short clientport;

module_param(myport, int, S_IRUGO);
MODULE_PARM_DESC(myport,"The receiving port, default 3002");

static udp_service * kproposer;
static struct socket * kpsocket;

int connection_handler(void *data)
{
  struct sockaddr_in address;
  struct socket *proposer_socket = kpsocket;

  int ret;
  unsigned char * in_buf = kmalloc(MAX_UDP_SIZE, GFP_KERNEL);
  // unsigned char * out_buf = kmalloc(strlen(), GFP_KERNEL);
  size_t size_buf, size_msg1, size_msg2, size_msg3;


  size_msg1 = strlen(VFC);
  size_msg2 = strlen(P1B);
  size_msg3 = strlen(A2B);

  while (1){

    if(kthread_should_stop() || signal_pending(current)){
      check_sock_allocation(kproposer, proposer_socket);
      kfree(in_buf);
      // kfree(out_buf);
      return 0;
    }

    memset(in_buf, '\0', MAX_UDP_SIZE);
    memset(&address, 0, sizeof(struct sockaddr_in));
    ret = udp_server_receive(proposer_socket, &address, in_buf, MSG_WAITALL, kproposer);
    if(ret > 0){
      size_buf = strlen(in_buf);
      if(memcmp(in_buf, VFC, size_buf > size_msg1 ? size_msg1 : size_buf) == 0){
        memcpy(&clientaddr, &address.sin_addr, sizeof(struct in_addr));
        unsigned short i = ntohs(address.sin_port);
        memcpy(&clientport, &i, sizeof(unsigned short));
        prepare_sockaddr(&address, acceptorport, NULL, acceptorip);
        udp_server_send(proposer_socket, &address, P1A, strlen(P1A), kproposer->name);
      }else if (memcmp(in_buf, P1B, size_buf > size_msg2 ? size_msg2 : size_buf) == 0){
        prepare_sockaddr(&address, acceptorport, NULL, acceptorip);
        udp_server_send(proposer_socket, &address, A2A, strlen(A2A), kproposer->name);
      } else if (memcmp(in_buf, A2B, size_buf > size_msg3 ? size_msg3 : size_buf) == 0){
        prepare_sockaddr(&address, clientport, &clientaddr, NULL);
        udp_server_send(proposer_socket, &address, AD, strlen(AD), kproposer->name);
      }
    }
  }

  return 0;
}

int udp_server_listen(void)
{
  udp_server_init(kproposer, &kpsocket, myip, &myport);
  connection_handler(NULL);
  atomic_set(&kproposer->thread_running, 0);
  return 0;
}

void udp_server_start(void){
  kproposer->u_thread = kthread_run((void *)udp_server_listen, NULL, kproposer->name);
  if(kproposer->u_thread >= 0){
    atomic_set(&kproposer->thread_running,1);
    printk(KERN_INFO "%s Thread running [udp_server_start]", kproposer->name);
  }else{
    printk(KERN_INFO "%s Error in starting thread. Terminated [udp_server_start]", kproposer->name);
  }
}

static int __init network_server_init(void)
{
  kproposer = kmalloc(sizeof(udp_service), GFP_KERNEL);
  if(!kproposer){
    printk(KERN_INFO "Failed to initialize server [network_server_init]");
  }else{
    init_service(kproposer, "Proposer:");
    udp_server_start();
  }
  return 0;
}

static void __exit network_server_exit(void)
{
  udp_server_quit(kproposer, kpsocket);
}

module_init(network_server_init)
module_exit(network_server_exit)
MODULE_LICENSE("MIT");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");
