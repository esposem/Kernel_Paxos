#include <asm/atomic.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/udp.h>
#include <net/sock.h>

#include "evpaxos.h"

static char *if_name = "enp0s3";
module_param(if_name, charp, 0000);
MODULE_PARM_DESC(if_name, "The interface name, default enp0s3");

static int id = 0;
module_param(id, int, S_IRUGO);
MODULE_PARM_DESC(id, "The acceptor id, default 0");

static struct evacceptor *acc = NULL;

static void start_acceptor(int id) {
  acc = evacceptor_init(id, if_name);
  if (acc == NULL) {
    printk(KERN_ERR "Could not start the acceptor\n");
  }
}

static int __init init_acceptor(void) {
  start_acceptor(id);
  return 0;
}

static void __exit acceptor_exit(void) {
  if (acc != NULL)
    evacceptor_free(acc);
  printk("Module unloaded\n\n");
}

module_init(init_acceptor) module_exit(acceptor_exit) MODULE_LICENSE("GPL");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");
