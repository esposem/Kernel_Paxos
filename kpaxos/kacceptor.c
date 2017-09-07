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

static void
start_acceptor(int id)
{
	acc = evacceptor_init(id, kacceptor);
	if (acc == NULL) {
		printk(KERN_ERR "%s Could not start the acceptor", kacceptor->name);
	}else{
		paxos_acceptor_listen(kacceptor, acc);
		evacceptor_free(acc);
	}
}


static int run_acceptor(void)
{
	start_acceptor(id);
	atomic_set(&kacceptor->thread_running, 0);
  return 0;
}

static void start_acc_thread(void){
  kacceptor->u_thread = kthread_run((void *)run_acceptor, NULL, kacceptor->name);
  if(kacceptor->u_thread >= 0){
    atomic_set(&kacceptor->thread_running,1);
    printk(KERN_INFO "%s Thread running", kacceptor->name);
  }else{
    printk(KERN_ERR "%s Error in starting thread", kacceptor->name);
  }
}

static int __init init_acceptor(void)
{
  kacceptor = kmalloc(sizeof(udp_service), GFP_ATOMIC | __GFP_REPEAT);
  if(!kacceptor){
    printk(KERN_ERR "Failed to initialize ACCEPTOR");
  }else{
    init_service(kacceptor, "Acceptor", id);
    start_acc_thread();
  }
  return 0;
}

static void __exit acceptor_exit(void)
{
  udp_server_quit(kacceptor);
}


module_init(init_acceptor)
module_exit(acceptor_exit)
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");
