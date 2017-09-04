#include <linux/module.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/udp.h>
#include <asm/atomic.h>
#include <linux/time.h>
#include <net/sock.h>

#include "evpaxos.h"
#include "kernel_udp.h"
#include "kernel_client.h"
// #include "kernel_device.h"

// struct file_operations fops =
// {
//   .open = kdev_open,
//   .read = kdev_read,
//   .write = kdev_write,
//   .release = kdev_release,
// };

static atomic_t rcv;

static int id = 0;
module_param(id, int, S_IRUGO);
MODULE_PARM_DESC(id,"The replica id, default 0");

static udp_service * kreplica;
static struct evpaxos_replica* replica = NULL;
struct timeval sk_timeout_timeval;

static void
deliver(unsigned iid, char* value, size_t size, void* arg)
{
	atomic_inc(&rcv);
	struct client_value* val = (struct client_value*)value;
	printk(KERN_INFO "%s: %ld.%06ld [%.16s] %ld bytes", kreplica->name, val->t.tv_sec, val->t.tv_usec, val->value, (long)val->size);
}

static void
start_replica(int id, const char* config)
{
	deliver_function cb = NULL;
	replica = evpaxos_replica_init(id, config, cb, deliver, kreplica);

	if (replica == NULL) {
		printk(KERN_ERR "Could not start the replica!");
	}else{
		paxos_replica_listen(kreplica, replica);
	}
	evpaxos_replica_free(replica);
}

static int run_replica(void)
{
  const char* config = "../paxos.conf";
  start_replica(id, config);
	atomic_set(&kreplica->thread_running, 0);
  return 0;
}

static void start_replica_thread(void){
  kreplica->u_thread = kthread_run((void *)run_replica, NULL, kreplica->name);
  if(kreplica->u_thread >= 0){
    atomic_set(&kreplica->thread_running,1);
		atomic_set(&rcv,0);
    printk(KERN_INFO "%s Thread running", kreplica->name);
  }else{
    printk(KERN_ERR "%s Error in starting thread.", kreplica->name);
  }
}

static int __init init_replica(void)
{
	if(id < 0 || id > 10){
		printk(KERN_ERR "you must give a valid id!");
		return 0;
	}
  kreplica = kmalloc(sizeof(udp_service), GFP_KERNEL);
  if(!kreplica){
    printk(KERN_ERR "Failed to initialize replica");
  }else{
    init_service(kreplica, "Replica", id);
    start_replica_thread();
  }
  return 0;
}

static void __exit replica_exit(void)
{
	printk(KERN_ERR "Received %d", atomic_read(&rcv));
  udp_server_quit(kreplica);
}

module_init(init_replica)
module_exit(replica_exit)
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");
