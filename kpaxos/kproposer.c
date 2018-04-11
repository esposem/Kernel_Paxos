#include "evpaxos.h"
#include <asm/atomic.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/udp.h>
#include <net/sock.h>

const char* MOD_NAME = "KPROPOSER";

static char* if_name = "enp4s0";
module_param(if_name, charp, 0000);
MODULE_PARM_DESC(if_name, "The interface name, default enp4s0");

static int id = 0;
module_param(id, int, S_IRUGO);
MODULE_PARM_DESC(id, "The proposer id, default 0");

static char* path = "./paxos.conf";
module_param(path, charp, S_IRUGO);
MODULE_PARM_DESC(path, "The config file position, default ./paxos.conf");

static struct evproposer* prop = NULL;

static void
start_proposer(int id)
{
  prop = evproposer_init(id, if_name, path);
  if (prop == NULL) {
    printk(KERN_ERR "Could not start the proposer\n");
  }
}

static int __init
           init_prop(void)
{
  start_proposer(id);
  return 0;
}

static void __exit
            prop_exit(void)
{
  if (prop != NULL)
    evproposer_free(prop);
  printk("Module unloaded\n\n");
}

module_init(init_prop);
module_exit(prop_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");
