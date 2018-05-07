#include "evpaxos.h"
#include "kernel_device.h"
#include <asm/atomic.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/udp.h>
#include <net/sock.h>

#define SEND_TO_CHAR_DEVICE 0
const char* MOD_NAME = "KReplica";

struct file_operations fops = {
  .open = kdev_open,
  .read = kdev_read,
  .write = kdev_write,
  .release = kdev_release,
};

static int id = 0;
module_param(id, int, S_IRUGO);
MODULE_PARM_DESC(id, "The replica id, default 0");

static char* if_name = "enp4s0";
module_param(if_name, charp, 0000);
MODULE_PARM_DESC(if_name, "The interface name, default enp4s0");

static char* path = "./paxos.conf";
module_param(path, charp, S_IRUGO);
MODULE_PARM_DESC(path, "The config file position, default ./paxos.conf");

static struct evpaxos_replica* replica = NULL;

void
deliver(unsigned iid, char* value, size_t size, void* arg)
{
  // struct client_value* val = (struct client_value*)value;
  // printk(KERN_INFO "%s: %ld.%06ld [%.16s] %ld bytes", kreplica->name,
  // val->t.tv_sec, val->t.tv_usec, val->value, (long)val->size);
  if (SEND_TO_CHAR_DEVICE)
    kset_message(value, size);
}

static void
start_replica(int id)
{
  // kdevchar_init(id, "klearner");
  replica = evpaxos_replica_init(id, deliver, NULL, if_name, path);

  if (replica == NULL) {
    LOG_ERROR("Could not start the replica!");
  }
}

static int __init
           init_replica(void)
{
  if (id < 0) {
    LOG_ERROR("you must give a valid id!");
    return 0;
  }
  start_replica(id);
  LOG_INFO("Module loaded");
  return 0;
}

static void __exit
            replica_exit(void)
{
  // kdevchar_exit();
  if (replica != NULL)
    evpaxos_replica_free(replica);
  LOG_INFO("Module unloaded");
}

module_init(init_replica);
module_exit(replica_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");
