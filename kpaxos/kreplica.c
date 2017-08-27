#include <linux/module.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/udp.h>
#include <asm/atomic.h>
#include <linux/time.h>
#include <net/sock.h>

#include "evpaxos.h"
#include "kernel_udp.h"
#include "kernel_device.h"

struct client_value
{
	int client_id;
	struct timeval t;
	size_t size;
	char value[0];
};

struct file_operations fops =
{
  .open = kdev_open,
  .read = kdev_read,
  .write = kdev_write,
  .release = kdev_release,
};

static int id = 0;
module_param(id, int, S_IRUGO);
MODULE_PARM_DESC(id,"The replica id, default 0");

static udp_service * kreplica;
static struct evpaxos_replica* replica = NULL;
struct timeval sk_timeout_timeval;

static void
deliver(unsigned iid, char* value, size_t size, void* arg)
{
	struct client_value* val = (struct client_value*)value;
	printk(KERN_INFO "%s: %ld.%06ld [%.16s] %ld bytes", kreplica->name, val->t.tv_sec, val->t.tv_usec, val->value, (long)val->size);
}

static void
start_replica(int id, const char* config)
{
	deliver_function cb = NULL;

	// if (verbose)
	// 	cb = deliver;

	replica = evpaxos_replica_init(id, config, cb, deliver, kreplica);

	if (replica == NULL) {
		// printk(KERN_INFO "Could not start the replica!");
	}else{
		paxos_replica_listen(kreplica, replica);
	}
	// printk(KERN_INFO "Called evpaxos_replica_free");
	evpaxos_replica_free(replica);
}

int udp_server_listen(void)
{
  const char* config = "../paxos.conf";
  start_replica(id, config);
	atomic_set(&kreplica->thread_running, 0);
  return 0;
}

void udp_server_start(void){
  kreplica->u_thread = kthread_run((void *)udp_server_listen, NULL, kreplica->name);
  if(kreplica->u_thread >= 0){
    atomic_set(&kreplica->thread_running,1);
    // printk(KERN_INFO "%s Thread running [udp_server_start]", kreplica->name);
  }else{
    // printk(KERN_INFO "%s Error in starting thread. Terminated [udp_server_start]", kreplica->name);
  }
}

static int __init network_server_init(void)
{
	if(id < 0 || id > 10){
		// printk(KERN_INFO "you must give an id!");
		return 0;
	}
  kreplica = kmalloc(sizeof(udp_service), GFP_KERNEL);
  if(!kreplica){
    // printk(KERN_INFO "Failed to initialize server [network_server_init]");
  }else{
    init_service(kreplica, "Replica", id);
    udp_server_start();
  }
  return 0;
}

static void __exit network_server_exit(void)
{
	if(replica != NULL)
		stop_replica_timer(replica);

  udp_server_quit(kreplica);
}

module_init(network_server_init)
module_exit(network_server_exit)
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");
