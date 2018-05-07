#include "evpaxos.h"
#include "kernel_device.h"
#include <asm/atomic.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/udp.h>
#include <net/sock.h>

struct file_operations fops = {
  .open = kdev_open,
  .read = kdev_read,
  .write = kdev_write,
  .release = kdev_release,
  .poll = kdev_poll,
};

const char* MOD_NAME = "KLearner";

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
on_deliver(unsigned int iid, char* value, size_t size, void* arg)
{
  kset_message(value, size);
}

static int
start_learner(void)
{
  kdevchar_init(id, "klearner");
  lea = evlearner_init(on_deliver, NULL, if_name, path, 0);

  if (lea == NULL) {
    LOG_ERROR("Could not start the learner!");
  }

  return 0;
}

static int __init
           init_learner(void)
{
  if (id < 0 || id > 10) {
    LOG_ERROR("you must give an id!");
    return 0;
  }
  start_learner();
  LOG_INFO("Module loaded");
  return 0;
}

static void __exit
            learner_exit(void)
{
  kdevchar_exit();
  if (lea != NULL)
    evlearner_free(lea);
  LOG_INFO("Module unloaded");
}

module_init(init_learner);
module_exit(learner_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");
