#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <net/udp.h>
#include <asm/atomic.h>
#include <linux/time.h>
#include <net/sock.h>

#include "kernel_udp.h"

static unsigned char myip[5] = {127,0,0,4,'\0'};
static int myport = 3000;
module_param(myport, int, S_IRUGO);
MODULE_PARM_DESC(myport,"The receiving port, default 3000");

static udp_service * klearner;
static struct socket * klsocket;
int connection_handler(void *data)
{
  struct sockaddr_in address;
  struct socket *learner_socket = klsocket;

  int ret;
  unsigned char * in_buf = kmalloc(MAX_UDP_SIZE, GFP_KERNEL);
  // unsigned char * out_buf= kmalloc(strlen(), GFP_KERNEL);

  while (1){

    if(kthread_should_stop() || signal_pending(current)){
      check_sock_allocation(klearner, learner_socket);
      kfree(in_buf);
      // kfree(out_buf);
      return 0;
    }

    memset(in_buf, '\0', MAX_UDP_SIZE);
    memset(&address, 0, sizeof(struct sockaddr_in));
    ret = udp_server_receive(learner_socket, &address, in_buf, MSG_WAITALL,klearner);

    if(ret > 0){
      if(memcmp(in_buf, A2B, 11) == 0){
        printk(KERN_INFO "%s GOT ACCEPTED 2B", klearner->name);
        return 0;
      }
    }
  }

  return 0;
}

int udp_server_listen(void)
{
  udp_server_init(klearner, &klsocket, myip, &myport);
  connection_handler(NULL);
  atomic_set(&klearner->thread_running, 0);
  return 0;
}

void udp_server_start(void){
  klearner->u_thread = kthread_run((void *)udp_server_listen, NULL, klearner->name);
  if(klearner->u_thread >= 0){
    atomic_set(&klearner->thread_running,1);
    printk(KERN_INFO "%s Thread running [udp_server_start]", klearner->name);
  }else{
    printk(KERN_INFO "%s Error in starting thread. Terminated [udp_server_start]", klearner->name);
  }
}

static int __init network_server_init(void)
{
  klearner = kmalloc(sizeof(udp_service), GFP_KERNEL);
  if(!klearner){
    printk(KERN_INFO "Failed to initialize server [network_server_init]");
  }else{
    init_service(klearner, "Learner:");
    udp_server_start();
  }
  return 0;
}

static void __exit network_server_exit(void)
{
  udp_server_quit(klearner, klsocket);
}

module_init(network_server_init)
module_exit(network_server_exit)
MODULE_LICENSE("MIT");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");
