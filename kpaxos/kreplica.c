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

static udp_service * kreplica;
static struct socket * krsocket;

static void
deliver(unsigned iid, char* value, size_t size, void* arg)
{
	struct client_value* val = (struct client_value*)value;
	printk(KERN_INFO "%s: %ld.%06ld [%.16s] %ld bytes", kreplica->name, val->t.tv_sec, val->t.tv_usec,
		val->value, (long)val->size);
}

static void
start_replica(int id, const char* config)
{
	struct evpaxos_replica* replica;
	deliver_function cb = NULL;

	if (verbose)
		cb = deliver;

	replica = evpaxos_replica_init(id, config, cb, NULL, base);

	if (replica == NULL) {
		printk(KERN_INFO "Could not start the replica!");
		return;
	}
	paxos_replica_listen(kreplica, replica)
	evpaxos_replica_free(replica);
}




int udp_server_listen(void)
{

  atomic_set(&kreplica->thread_running, 0);
  const char* config = "../paxos.conf";
  start_learner(config);
  return 0;
}

void udp_server_start(void){
  kreplica->u_thread = kthread_run((void *)udp_server_listen, NULL, kreplica->name);
  if(kreplica->u_thread >= 0){
    atomic_set(&kreplica->thread_running,1);
    printk(KERN_INFO "%s Thread running [udp_server_start]", kreplica->name);
  }else{
    printk(KERN_INFO "%s Error in starting thread. Terminated [udp_server_start]", kreplica->name);
  }
}

static int __init network_server_init(void)
{
  kreplica = kmalloc(sizeof(udp_service), GFP_KERNEL);
  if(!kreplica){
    printk(KERN_INFO "Failed to initialize server [network_server_init]");
  }else{
    init_service(kreplica, "Learner:");
    udp_server_start();
  }
  return 0;
}

static void __exit network_server_exit(void)
{
  udp_server_quit(kreplica, krsocket);
}

module_init(network_server_init)
module_exit(network_server_exit)
MODULE_LICENSE("MIT");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");
