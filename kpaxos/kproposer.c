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
static struct evproposer* prop = NULL;

static int id = 0;
module_param(id, int, S_IRUGO);
MODULE_PARM_DESC(id,"The proposer id, default 0");

static void
start_proposer(int id)
{
	prop = evproposer_init(id, kproposer);
	if (prop == NULL) {
		printk(KERN_ERR "%s Could not start the proposer!", kproposer->name);
	}else{
		paxos_proposer_listen(kproposer, prop);
		evproposer_free(prop);
	}
}

static int run_proposer(void)
{
  start_proposer(id);
	atomic_set(&kproposer->thread_running, 0);
  return 0;
}

static void start_prop_thread(void){
  kproposer->u_thread = kthread_run((void *)run_proposer, NULL, kproposer->name);
  if(kproposer->u_thread >= 0){
    atomic_set(&kproposer->thread_running,1);
    printk(KERN_INFO "%s Thread running", kproposer->name);
  }else{
    printk(KERN_ERR "%s Error in starting thread", kproposer->name);
  }
}

static int __init init_prop(void)
{
  kproposer = kmalloc(sizeof(udp_service), GFP_KERNEL);
  if(!kproposer){
    printk(KERN_ERR "Failed to initialize server");
  }else{
    init_service(kproposer, "Proposer", id);
    start_prop_thread();
  }
  return 0;
}

static void __exit prop_exit(void)
{
  udp_server_quit(kproposer);
}

module_init(init_prop)
module_exit(prop_exit)
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");
