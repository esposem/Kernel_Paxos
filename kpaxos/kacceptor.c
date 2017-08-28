#include <linux/module.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/udp.h>
#include <asm/atomic.h>
#include <linux/time.h>
#include <net/sock.h>

#include "evpaxos.h"
#include "kernel_udp.h"

static int id = 0;
module_param(id, int, S_IRUGO);
MODULE_PARM_DESC(id,"The acceptor id, default 0");

static udp_service * kacceptor;
static struct evacceptor* acc = NULL;
struct timeval sk_timeout_timeval;

static void
start_acceptor(int id, const char* config)
{
	acc = evacceptor_init(id, config, kacceptor);
	if (acc == NULL) {
		// printk(KERN_INFO "%s Could not start the acceptor", kacceptor->name);
	}else{
		paxos_acceptor_listen(kacceptor, acc);
	}
	// printk(KERN_INFO "Called evacceptor_free");
	evacceptor_free(acc);
}


int udp_server_listen(void)
{
  const char* config = "../paxos.conf";
	start_acceptor(id, config);
	atomic_set(&kacceptor->thread_running, 0);
  return 0;
}

void udp_server_start(void){
  kacceptor->u_thread = kthread_run((void *)udp_server_listen, NULL, kacceptor->name);
  if(kacceptor->u_thread >= 0){
    atomic_set(&kacceptor->thread_running,1);
    // printk(KERN_INFO "%s Thread running [udp_server_start]", kacceptor->name);
  }else{
    // printk(KERN_INFO "%s Error in starting thread. Terminated [udp_server_start]", kacceptor->name);
  }
}

static int __init network_server_init(void)
{
	if(id < 0 || id > 10){
		// printk(KERN_INFO "you must give an id!");
		return 0;
	}
	//  ;
  kacceptor = kmalloc(sizeof(udp_service), GFP_KERNEL);
  if(!kacceptor){
    // printk(KERN_INFO "Failed to initialize ACCEPTOR [network_server_init]");
  }else{
    init_service(kacceptor, "Acceptor", id);
    udp_server_start();
  }
  return 0;
}

static void __exit network_server_exit(void)
{
  udp_server_quit(kacceptor);
}


module_init(network_server_init)
module_exit(network_server_exit)
MODULE_LICENSE("MIT");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");
