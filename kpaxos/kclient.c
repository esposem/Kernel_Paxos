#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <net/udp.h>
#include <asm/atomic.h>
#include <linux/time.h>
#include <net/sock.h>

#include "kernel_udp.h"

static unsigned char proposerip[5] = {127,0,0,2,'\0'};
static int proposerport = 3002;
static unsigned char myip[5] = {127,0,0,1,'\0'};
static int myport = 3001;

module_param(myport, int, S_IRUGO);
MODULE_PARM_DESC(myport,"The receiving port, default 3001");

static udp_service * kclient;
static struct socket * kcsocket;

int connection_handler(void *data)
{
  struct sockaddr_in address;
  struct socket *client_socket = kcsocket;

  int ret;
  unsigned char * in_buf = kmalloc(MAX_UDP_SIZE, GFP_KERNEL);
  unsigned char * out_buf = kmalloc(strlen(VFC), GFP_KERNEL);
  size_t size_msg, size_buf;
  prepare_sockaddr(&address, proposerport, NULL, proposerip);
  udp_server_send(client_socket,&address, VFC, strlen(VFC), kclient->name);

  while (1){

    if(kthread_should_stop() || signal_pending(current)){
      check_sock_allocation(kclient, client_socket);
      kfree(in_buf);
      kfree(out_buf);
      return 0;
    }


    memset(in_buf, '\0', MAX_UDP_SIZE);
    memset(&address, 0, sizeof(struct sockaddr_in));
    ret = udp_server_receive(client_socket, &address, in_buf, MSG_WAITALL, kclient);
    if(ret > 0){
      size_msg = strlen(AD);
      size_buf = strlen(in_buf);
      if(memcmp(in_buf, AD, size_buf > size_msg ? size_msg : size_buf) == 0){
        printk(KERN_INFO "%s All done, terminating client [connection_handler]", kclient->name);
      }
    }
  }

  return 0;
}

int udp_server_listen(void)
{
  udp_server_init(kclient, &kcsocket, myip, &myport);
  connection_handler(NULL);
  atomic_set(&kclient->thread_running, 0);
  return 0;
}

void udp_server_start(void){
  kclient->u_thread = kthread_run((void *)udp_server_listen, NULL, kclient->name);
  if(kclient->u_thread >= 0){
    atomic_set(&kclient->thread_running,1);
    printk(KERN_INFO "%s Thread running [udp_server_start]", kclient->name);
  }else{
    printk(KERN_INFO "%s Error in starting thread. Terminated [udp_server_start]", kclient->name);
  }
}

static int __init network_server_init(void)
{
  kclient = kmalloc(sizeof(udp_service), GFP_KERNEL);
  if(!kclient){
    printk(KERN_INFO "Failed to initialize CLIENT [network_server_init]");
  }else{
    init_service(kclient, "Client:");
    udp_server_start();
  }
  return 0;
}

static void __exit network_server_exit(void)
{
  udp_server_quit(kclient, kcsocket);
}

module_init(network_server_init)
module_exit(network_server_exit)
MODULE_LICENSE("MIT");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");
