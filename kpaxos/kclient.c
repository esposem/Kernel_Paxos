#include "evpaxos_internal.h"
#include "kernel_client.h"
#include "stats.h"
#include <asm/atomic.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/time.h>
#include <linux/udp.h>
#include <linux/vmalloc.h>
#include <net/sock.h>

#define TIMEOUT_US 1000000

const char* MOD_NAME = "KClient";

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

static int nclients = 1;
module_param(nclients, int, S_IRUGO);
MODULE_PARM_DESC(nclients, "The number of virtual clients");

static char* if_name = "enp4s0";
module_param(if_name, charp, 0000);
MODULE_PARM_DESC(if_name, "The interface name, default enp4s0");

static char* path = "./paxos.conf";
module_param(path, charp, S_IRUGO);
MODULE_PARM_DESC(path, "The config file position, default ./paxos.conf");

struct client
{
  struct timeval*      clients_timeval;
  eth_address          proposeradd[ETH_ALEN];
  struct client_value* val;
  char                 send_buffer[ETH_DATA_LEN];
  int                  send_buffer_len;
  struct stats         stats;
  struct timer_list    stats_ev;
  struct timeval       stats_interval;
  struct evlearner*    learner;
};

static struct client* c = NULL;

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
random_string(char* s, const int len)
{
  int               i, j;
  static const char alphanum[] = "0123456789abcdefghijklmnopqrstuvwxyz";

  for (i = 0, j = 0; i < len - 1; ++i, j = 0) {
    get_random_bytes(&j, sizeof(int));
    j &= 0xffffff;
    s[i] = alphanum[j % (sizeof(alphanum) - 1)];
  }

  s[len - 1] = 0;
}

static void
client_submit_value(struct client* c, int cid)
{
  c->val->client_id = cid;
  do_gettimeofday(&c->clients_timeval[cid - id]);
  c->val->t = c->clients_timeval[cid - id];
  paxos_submit(evlearner_get_device(c->learner), c->proposeradd, c->send_buffer,
               c->send_buffer_len);
  // LOG_DEBUG("Client %d submitted value %.16s with %zu bytes, total size is
  // %d",
  //           c->val->client_id, c->val->value, c->val->size,
  //           c->send_buffer_len);
}

static void
update_stats(struct stats* stats, struct client_value* delivered, size_t size)
{
  struct timeval tv;
  do_gettimeofday(&tv);
  long lat = timeval_diff(&delivered->t, &tv);
  stats->delivered_count++;
  stats_add(lat);
}

static void
check_timeout(void)
{
  struct timeval now;
  do_gettimeofday(&now);
  for (int i = 0; i < nclients; i++) {
    if (timeval_diff(&c->clients_timeval[i], &now) > TIMEOUT_US) {
      LOG_ERROR("Client %d sent expired", i);
      client_submit_value(c, i + id);
    }
  }
}

static void
on_deliver(unsigned iid, char* value, size_t size, void* arg)
{
  struct client*       c = arg;
  struct client_value* v = (struct client_value*)value;
  int                  clid = v->client_id;

  if (clid >= id && clid < id + nclients) {
    update_stats(&c->stats, v, size);
    //    LOG_DEBUG(
    //      "Client %d received value %.16s with %zu bytes, total size is %zu",
    //      v->client_id, v->value, v->size, size);
    client_submit_value(c, clid);
  }
}

static void
on_stats(struct timer_list *t)
{
  struct client* c = from_timer(c, t, stats_ev);
  long           mbps =
    (c->stats.delivered_count * c->send_buffer_len * 8) / (1024 * 1024);

  LOG_INFO("%d val/sec, %ld Mbps", c->stats.delivered_count, mbps);
  memset(&c->stats, 0, sizeof(struct stats));
  check_timeout();
  mod_timer(&c->stats_ev, jiffies + timeval_to_jiffies(&c->stats_interval));
}

void
start_client(int proposer_id, int value_size)
{
  struct evpaxos_config* conf = evpaxos_config_read(path);

  if (conf == NULL) {
    LOG_ERROR("Failed to read config file.");
    return;
  }

  c = pmalloc(sizeof(struct client));
  c->learner = evlearner_init(on_deliver, c, if_name, path, 1);
  if (c->learner == NULL) {
    LOG_ERROR("Could not start the learner.");
    kfree(c);
    c = NULL;
    return;
  }

  c->clients_timeval = vmalloc(sizeof(struct timeval) * nclients);
  memset(c->clients_timeval, 0, sizeof(struct timeval) * nclients);
  memset(&c->stats, 0, sizeof(struct stats));
  memcpy(c->proposeradd, evpaxos_proposer_address(conf, proposer_id), ETH_ALEN);
  evpaxos_config_free(conf);

  c->val = (struct client_value*)c->send_buffer;
  c->val->size = value_size;
  c->send_buffer_len = sizeof(struct client_value) + value_size;
  random_string(c->val->value, value_size);

  timer_setup(&c->stats_ev, on_stats, 0);
  c->stats_interval = (struct timeval){ 1, 0 };
  mod_timer(&c->stats_ev, jiffies + timeval_to_jiffies(&c->stats_interval));

  stats_init();
  for (int i = 0; i < nclients; ++i) {
    client_submit_value(c, id + i);
  }
}

static int __init
           init_client(void)
{
  LOG_INFO("Id: %d --- Nclients: %d", id, nclients);
  start_client(proposer_id, value_size);
  return 0;
}

static void __exit
            client_exit(void)
{
  char file_name[128];
  int  id = 0;

  if (c != NULL) {
    if (c->learner) {
      del_timer(&c->stats_ev);
      evlearner_free(c->learner);
    }
    vfree(c->clients_timeval);
    pfree(c);
  }

  stats_print();
  get_random_bytes(&id, sizeof(id));
  id &= 0xffffff;
  sprintf(file_name, "stats-%3.3dclients-%d.txt", nclients, id);
  stats_persist(file_name);
  stats_destroy();
  LOG_INFO("Module unloaded.");
}

module_init(init_client);
module_exit(client_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");
