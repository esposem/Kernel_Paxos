#include <asm/atomic.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/udp.h>
#include <net/sock.h>

#include "evpaxos.h"

#include "kernel_client.h"
#include "kernel_device.h"

struct file_operations fops = {
    .open = kdev_open,
    .read = kdev_read,
    .write = kdev_write,
    .release = kdev_release,
};

static int id = 0;
module_param(id, int, S_IRUGO);
MODULE_PARM_DESC(id, "The replica id, default 0");

size_t sendtrim;
atomic_t auto_trim;
static int cantrim = 0;
module_param(cantrim, int, S_IRUGO);
MODULE_PARM_DESC(cantrim, "If the module has to send trim, set it to 1");

static char *if_name = "enp0s3";
module_param(if_name, charp, 0000);
MODULE_PARM_DESC(if_name, "The interface name, default enp0s3");

static struct evpaxos_replica *replica = NULL;

void deliver(unsigned iid, char *value, size_t size, void *arg) {

  // struct client_value* val = (struct client_value*)value;
  // printk(KERN_INFO "%s: %ld.%06ld [%.16s] %ld bytes", kreplica->name,
  // val->t.tv_sec, val->t.tv_usec, val->value, (long)val->size);

  if (atomic_read(&auto_trim) == 1) {
    if (sendtrim > 0) {
      if (cantrim > 0) {
        printk(KERN_ERR "Replica sent trim to all");
        evpaxos_replica_send_trim(replica, sendtrim);
      }
      printk("Replica sent autotrim");
      evpaxos_replica_internal_trim(replica, sendtrim);
      sendtrim = 0;
    }
  } else {
    if (iid % 100000 == 0) {
      printk("Replica sent indipendent autotrim");
      evpaxos_replica_internal_trim(replica, iid - 100000 + 1);
    }
  }

  kset_message(value, size, iid);
}

static void start_replica(int id) {

  kdevchar_init(id, "klearner");
  replica = evpaxos_replica_init(id, deliver, NULL, if_name);

  if (replica == NULL) {
    printk(KERN_ERR "Could not start the replica!");
  }
}

static int __init init_replica(void) {
  if (id < 0 || id > 10) {
    printk(KERN_ERR "you must give a valid id!");
    return 0;
  }
  start_replica(id);
  return 0;
}

static void __exit replica_exit(void) {
  kstop_device();
  kdevchar_exit();
  if (replica != NULL)
    evpaxos_replica_free(replica);
  printk("Module unloaded\n\n");
}

module_init(init_replica) module_exit(replica_exit) MODULE_LICENSE("GPL");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");
