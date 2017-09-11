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
#include "kernel_device.h"

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

size_t sendtrim;
static int cantrim = 0;
module_param(cantrim, int, S_IRUGO);
MODULE_PARM_DESC(cantrim,"If the module has to send trim, set it to 1");


static udp_service * kreplica;
static struct evpaxos_replica* replica = NULL;

void deliver(unsigned iid, char* value, size_t size, void* arg)
{

	struct client_value* val = (struct client_value*)value;
	// printk(KERN_INFO "%s: %ld.%06ld [%.16s] %ld bytes", kreplica->name, val->t.tv_sec, val->t.tv_usec, val->value, (long)val->size);

	if(sendtrim > 0){
    if(cantrim > 0){
			printk(KERN_ERR "%s sent trim to all", kreplica->name);
      evpaxos_replica_send_trim(replica, sendtrim);
    }
		printk("%s sent autotrim", kreplica->name);
    evpaxos_replica_internal_trim(replica, sendtrim);
    sendtrim = 0;
  }else{ // no learner in user space, I must trim
		if(iid % 100000 == 0){
			printk("%s sent autotrim", kreplica->name);
			evpaxos_replica_internal_trim(replica, iid- 100000 + 1);
		}
	}
	kset_message(value, size, iid);


}

static void
start_replica(int id)
{
	deliver_function cb = deliver;
	replica = evpaxos_replica_init(id, cb, NULL, kreplica);

	if (replica == NULL) {
		printk(KERN_ERR "Could not start the replica!");
	}else{
		paxos_replica_listen(kreplica, replica);
		evpaxos_replica_free(replica);
	}
}

static int run_replica(void)
{
	kdevchar_init(id, "klearner");

  start_replica(id);
	atomic_set(&kreplica->thread_running, 0);
  return 0;
}

static void start_replica_thread(void){
  kreplica->u_thread = kthread_run((void *)run_replica, NULL, kreplica->name);
  if(kreplica->u_thread >= 0){
    atomic_set(&kreplica->thread_running,1);
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
  kreplica = kmalloc(sizeof(udp_service), GFP_ATOMIC | __GFP_REPEAT);
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
	kstop_device();
	kdevchar_exit();
  udp_server_quit(kreplica);
}

module_init(init_replica)
module_exit(replica_exit)
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");
