#include <asm/atomic.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/udp.h>
#include <net/sock.h>

#include "evpaxos.h"
#include "paxos.h"

#include "kernel_client.h"
#include "kernel_device.h"

struct file_operations fops = {
  .open = kdev_open,
  .read = kdev_read,
  .write = kdev_write,
  .release = kdev_release,
};

size_t   sendtrim;
atomic_t auto_trim;

static int cantrim = 0;
module_param(cantrim, int, S_IRUGO);
MODULE_PARM_DESC(cantrim, "If the module has send to trim, set it to 1");

// static int catch_up = 0;
// module_param(catch_up, int, S_IRUGO);
// MODULE_PARM_DESC(
//   catch_up, "If the module has to catch up the previous values, set it to
//   1");

static int id = 0;
module_param(id, int, S_IRUGO);
MODULE_PARM_DESC(id, "The learner id (used for kdevice), default 0");

static char* if_name = "enp4s0";
module_param(if_name, charp, 0000);
MODULE_PARM_DESC(if_name, "The interface name, default enp4s0");

static char* path = "./paxos.conf";
module_param(path, charp, S_IRUGO);
MODULE_PARM_DESC(path, "The config file position, default ./paxos.conf");

static struct evlearner* lea = NULL;

static void
on_deliver(unsigned iid, char* value, size_t size, void* arg)
{
  if (atomic_read(&auto_trim) == 1) {
    if (sendtrim > 0) {
      if (cantrim > 0) {
        printk(KERN_ERR "Learner: sent trim to all\n");
        evlearner_send_trim(lea, sendtrim);
      }
      printk("Learner: sent autotrim");
      evlearner_auto_trim(lea, sendtrim);
      sendtrim = 0;
    }
  } else {
    if (iid % 100000 == 0) {
      printk("Learner: sent indipendent autotrim\n");
      evlearner_auto_trim(lea, iid - 100000 + 1);
    }
  }

  // printk(KERN_INFO "%s On deliver iid:%d size %zu ",klearner->name, iid,
  // size);
  kset_message(value, size, iid);
}

static int
start_learner(void)
{
  kdevchar_init(id, "klearner");

  // if (catch_up == 0) {
  //   paxos_config.learner_catch_up = 0;
  // }

  lea = evlearner_init(on_deliver, NULL, if_name, path, 0);

  if (lea == NULL) {
    printk(KERN_ERR "Could not start the learner!\n");
  }

  return 0;
}

static int __init
           init_learner(void)
{
  if (id < 0 || id > 10) {
    printk(KERN_ERR "you must give an id!\n");
    return 0;
  }

  // if (catch_up != 1 && catch_up != 0) {
  //   printk(KERN_ERR "invalid catch_up, set to 0\n");
  //   catch_up = 0;
  // }
  start_learner();
  return 0;
}

static void __exit
            learner_exit(void)
{
  kstop_device();
  kdevchar_exit();
  if (lea != NULL)
    evlearner_free(lea);
  printk("Module unloaded\n\n");
}

module_init(init_learner);
module_exit(learner_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");
