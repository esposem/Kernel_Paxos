#include <linux/module.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/udp.h>
#include <asm/atomic.h>
#include <linux/time.h>
#include <net/sock.h>

#include "paxos.h"
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

static int cantrim = 100000;
module_param(cantrim, int, S_IRUGO);
MODULE_PARM_DESC(cantrim,"If the module has to trim, set it to trim value");

static int catch_up = 0;
module_param(catch_up, int, S_IRUGO);
MODULE_PARM_DESC(catch_up,"If the module has to trim, set it to trim value");

static int id = 0;
module_param(id, int, S_IRUGO);
MODULE_PARM_DESC(id,"The learner id, default 0");

static udp_service * klearner;
static struct evlearner* lea = NULL;
// static atomic_t rcv;

static void
on_deliver(unsigned iid, char* value, size_t size, void* arg)
{
	// atomic_inc(&rcv);

  if(cantrim > 0 && (iid % cantrim == 0)){
    paxos_log_info("Called trim, instance %d ", iid - cantrim + 1);
    evlearner_send_trim(lea, iid - cantrim + 1);
  }

  // printk(KERN_INFO "%s On deliver iid:%d size %zu ",klearner->name, iid, size);
	kset_message(value, size, iid);
}

static void
start_learner(void)
{
	if(catch_up == 0){
		paxos_config.learner_catch_up = 0;
	}

	lea = evlearner_init(on_deliver, NULL, klearner);

	if (lea == NULL) {
		printk(KERN_ERR "%s:Could not start the learner!", klearner->name);
	}else{
		paxos_learner_listen(klearner, lea);
		evlearner_free(lea);
	}
}

static int run_learner(void)
{
	if(id < 0 || id > 10){
		printk(KERN_ERR "you must give an id!");
		return 0;
	}

	if(catch_up != 1 && catch_up != 0){
		printk(KERN_ERR "invalid catch_up, set to 0");
		catch_up = 0;
	}

	kdevchar_init(id, "klearner");

	// atomic_set(&rcv,0);
  start_learner();
	atomic_set(&klearner->thread_running, 0);
  return 0;
}

static void start_learner_thread(void){
  klearner->u_thread = kthread_run((void *)run_learner, NULL, klearner->name);
  if(klearner->u_thread >= 0){
    atomic_set(&klearner->thread_running,1);
    printk(KERN_INFO "%s Thread running", klearner->name);
  }else{
    printk(KERN_ERR "%s Error in starting thread", klearner->name);
  }
}

static int __init init_learner(void)
{
  klearner = kmalloc(sizeof(udp_service), GFP_ATOMIC | __GFP_REPEAT);
  if(!klearner){
    printk(KERN_ERR "Failed to initialize server");
  }else{
    init_service(klearner, "Learner", id);
    start_learner_thread();
  }
  return 0;
}

static void __exit learner_exit(void)
{
	kstop_device();
	kdevchar_exit();
	// printk(KERN_ERR "Received %d", atomic_read(&rcv));
  udp_server_quit(klearner);
}

module_init(init_learner)
module_exit(learner_exit)
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");
