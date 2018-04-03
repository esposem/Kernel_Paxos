#include <asm/atomic.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/time.h>
#include <linux/udp.h>
#include <net/sock.h>

#include "evpaxos.h"
#include "paxos.h"

#include "kernel_client.h"

static int proposer_id = 0;
module_param(proposer_id, int, S_IRUGO);
MODULE_PARM_DESC(proposer_id, "The proposer id, default 0");

static int outstanding = 1;
module_param(outstanding, int, S_IRUGO);
MODULE_PARM_DESC(outstanding, "The client outstanding, default 1");

static int value_size = 64;
module_param(value_size, int, S_IRUGO);
MODULE_PARM_DESC(value_size, "The size of the message, default 64");

static int id = 0;
module_param(id, int, S_IRUGO);
MODULE_PARM_DESC(id, "The client id, default 0");

static char* if_name = "enp1s0";
module_param(if_name, charp, 0000);
MODULE_PARM_DESC(if_name, "The interface name, default enp1s0");

static char* path = "./paxos.conf";
module_param(path, charp, S_IRUGO);
MODULE_PARM_DESC(path, "The config file position, default ./paxos.conf");

static struct client* c = NULL;
struct timeval        sk_timeout_timeval;

struct client
{
  int               id;
  eth_address       proposeradd[ETH_ALEN];
  int               value_size;
  int               outstanding;
  char*             send_buffer;
  struct stats      stats;
  struct timer_list stats_ev;
  struct timeval    stats_interval;
  struct evlearner* learner;
};

static void
random_string(char* s, const int len)
{
  int               i, j;
  static const char alphanum[] = "0123456789abcdefghijklmnopqrstuvwxyz";
  for (i = 0; i < len - 1; ++i) {
    get_random_bytes(&j, (sizeof j));
    s[i] = alphanum[j % (sizeof(alphanum) - 1)];
  }

  s[len - 1] = 0;
}

static void
client_submit_value(struct client* c)
{
  struct client_value* v = (struct client_value*)c->send_buffer;
  v->client_id = c->id;
  do_gettimeofday(&v->t);
  v->size = c->value_size;
  random_string(v->value, v->size);
  size_t size = sizeof(struct client_value) + v->size;
  paxos_submit(get_learn_dev(c->learner), c->proposeradd, c->send_buffer, size);
  paxos_log_debug("Client submitted PAXOS_CLIENT_VALUE len %zu value %s",
                  v->size, v->value);
}

static long
timeval_diff(struct timeval* t1, struct timeval* t2)
{
  long us;
  us = (t2->tv_sec - t1->tv_sec) * 1000000;
  if (us < 0)
    return 0;
  us += (t2->tv_usec - t1->tv_usec);
  return us;
}

static void
update_stats(struct stats* stats, struct client_value* delivered, size_t size)
{
  struct timeval tv;
  do_gettimeofday(&tv);
  long lat = timeval_diff(&delivered->t, &tv);
  stats->delivered_count++;
  stats->delivered_bytes += size;
  stats->avg_latency =
    stats->avg_latency + ((lat - stats->avg_latency) / stats->delivered_count);
  if (stats->min_latency == 0 || lat < stats->min_latency)
    stats->min_latency = lat;
  if (lat > stats->max_latency)
    stats->max_latency = lat;
}

static void
on_deliver(unsigned iid, char* value, size_t size, void* arg)
{
  struct client*       c = arg;
  struct client_value* v = (struct client_value*)value;

  if (v->client_id == c->id) {
    update_stats(&c->stats, v, size);
    if (iid % 100000 == 0) {
      paxos_log_info("Client%d: trim called, instance %d ", c->id, iid);
      evlearner_send_trim(c->learner, iid - 100000 + 1);
    }
    client_submit_value(c);
    // paxos_log_info(KERN_INFO "Client: On deliver iid:%d value:%.16s", iid,
    //                v->value);
  }
}

static void
on_stats(unsigned long arg)
{
  struct client* c = (struct client*)arg;
  int            mbps = (int)(c->stats.delivered_bytes * 8) / (1024 * 1024);
  printk(KERN_INFO "Client: %d value/sec, %d Mbps, latency min %ld us max %ld "
                   "us avg %ld us\n",
         c->stats.delivered_count, mbps, c->stats.min_latency,
         c->stats.max_latency, c->stats.avg_latency);
  memset(&c->stats, 0, sizeof(struct stats));
  mod_timer(&c->stats_ev, jiffies + timeval_to_jiffies(&c->stats_interval));
}

static struct client*
make_client(int proposer_id, int outstanding, int value_size)
{

  c = pmalloc(sizeof(struct client));

  memset(&c->stats, 0, sizeof(struct stats));
  struct evpaxos_config* conf = evpaxos_config_read(path);
  if (conf == NULL) {
    printk(KERN_ERR "Client: Failed to read config file\n");
    return NULL;
  }
  // c->proposeradd = evpaxos_proposer_address(conf, proposer_id);
  memcpy(c->proposeradd, evpaxos_proposer_address(conf, proposer_id), eth_size);
  // get_random_bytes(&c->id, sizeof(int));
  c->id = id;

  c->value_size = value_size;
  c->outstanding = outstanding;
  c->send_buffer = pmalloc(sizeof(struct client_value) + value_size);

  paxos_config.learner_catch_up = 0;
  c->learner = evlearner_init(on_deliver, c, if_name, path);

  if (c->learner == NULL) {
    printk(KERN_ERR "Client: Could not start the learner!");
  } else {
    setup_timer(&c->stats_ev, on_stats, (unsigned long)c);
    c->stats_interval = (struct timeval){ 1, 0 };
    mod_timer(&c->stats_ev, jiffies + timeval_to_jiffies(&c->stats_interval));

    for (int i = 0; i < c->outstanding; ++i)
      client_submit_value(c);
  }

  return c;
}

static void
client_free(struct client* c)
{

  kfree(c->send_buffer);
  if (c->learner) {
    del_timer(&c->stats_ev);
    evlearner_free(c->learner);
  }
  kfree(c);
}

static int __init
           init_client(void)
{
  if (id < 0 || id > 10) {
    printk(KERN_ERR "you must give an id!");
    return 0;
  }
  c = make_client(proposer_id, outstanding, value_size);
  return 0;
}

static void __exit
            client_exit(void)
{
  if (c != NULL) {
    client_free(c);
  }
  printk("Module unloaded\n\n");
}

module_init(init_client) module_exit(client_exit) MODULE_LICENSE("GPL");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");
