#include <linux/module.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/udp.h>
#include <asm/atomic.h>
#include <linux/time.h>
#include <net/sock.h>

#include "evpaxos.h"
#include "kernel_udp.h"

static udp_service * kproposer;

static int id = 0;
module_param(id, int, S_IRUGO);
MODULE_PARM_DESC(id,"The proposer id, default 0");

struct evproposer* prop = NULL;

static void
start_proposer(const char* config, int id)
{

	prop = evproposer_init(id, config, kproposer);
	if (prop == NULL) {
		printk(KERN_INFO "%s: Could not start the proposer!", kproposer->name);
	}else{
		paxos_proposer_listen(kproposer, prop);
	}
	evproposer_free(prop);
}

int udp_server_listen(void)
{
  const char* config = "../paxos.conf";
  start_proposer(config, id);
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
	if(prop != NULL)
		stop_proposer_timer(prop);
  udp_server_quit(kproposer);
}

module_init(network_server_init)
module_exit(network_server_exit)
MODULE_LICENSE("MIT");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");
