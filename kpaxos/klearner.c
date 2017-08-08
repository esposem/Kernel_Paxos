#include <linux/module.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/udp.h>
#include <asm/atomic.h>
#include <linux/time.h>
#include <net/sock.h>

#include "evpaxos.h"
#include "kernel_udp.h"

struct client_value
{
	int client_id;
	struct timeval t;
	size_t size;
	char value[0];
};

static udp_service * klearner;
static struct socket * klsocket;



static void
deliver(unsigned iid, char* value, size_t size, void* arg)
{
	struct client_value* val = (struct client_value*)value;
	printk(KERN_INFO "%s:Learner: %ld.%06ld [%.16s] %ld bytes", klearner->name, val->t.tv_sec, val->t.tv_usec,
		val->value, (long)val->size);
}

static void
start_learner(const char* config)
{
	struct evlearner* lea;

	lea = evlearner_init(config, deliver, NULL, klearner);
	if (lea == NULL) {
		printk(KERN_INFO "%s:Could not start the learner!", klearner->name);
		return;
	}
  paxos_learner_listen(klearner, lea);

	evlearner_free(lea);

}




int udp_server_listen(void)
{

  atomic_set(&klearner->thread_running, 0);
  const char* config = "../paxos.conf";
  start_learner(config);
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
