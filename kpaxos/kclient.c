#include "evpaxos_internal.h"
#include "kernel_client.h"
#include "paxos.h"
#include <asm/atomic.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/time.h>
#include <linux/udp.h>
#include <net/sock.h>

#define TIMEOUT_US 1000000

const char* MOD_NAME = "KCLIENT";

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

static int trimval = 500000;
module_param(trimval, int, S_IRUGO);
MODULE_PARM_DESC(trimval, "After how many instance should the klearner trim");

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
  int               i, j = 0;
  static const char alphanum[] = "0123456789abcdefghijklmnopqrstuvwxyz";

  for (i = 0; i < len - 1; ++i, j = 0) {
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
  paxos_log_debug(
    "Client %d submitted value %.16s with %zu bytes, total size is %zu",
    c->val->client_id, c->val->value, c->val->size, c->send_buffer_len);
}

static void
update_stats(struct stats* stats, struct client_value* delivered, size_t size,
             struct timeval* tv)
{
  // do_gettimeofday(tv);
  // long lat = timeval_diff(&delivered->t, tv);
  stats->delivered_count++;

  /* // TODO LATER uncomment this
  stats->delivered_bytes += size;
  stats->avg_latency =
    stats->avg_latency + ((lat - stats->avg_latency) /
    stats->delivered_count);
  if (stats->min_latency == 0 || lat < stats->min_latency)
    stats->min_latency = lat;
  if (lat > stats->max_latency)
    stats->max_latency = lat;
  */
}

static void
check_timeout(struct timeval* now)
{
  for (int i = 0; i < nclients; i++) {
    if (timeval_diff(&c->clients_timeval[i], now) > TIMEOUT_US) {
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
  struct timeval       now = { 0, 0 };

  if (clid >= id && clid < id + nclients) {
    update_stats(&c->stats, v, size, &now);
    if (iid % trimval == 0) {
      paxos_log_info("Client%d: trim called, instance %d ", clid, iid);
      evlearner_send_trim(c->learner, iid - trimval + 1);
    }
    paxos_log_debug(
      "Client %d received value %.16s with %zu bytes, total size is %zu",
      v->client_id, v->value, v->size, size);

    client_submit_value(c, clid);
  }
}

static void
on_stats(unsigned long arg)
{
  struct client* c = (struct client*)arg;
  struct timeval now;
  long           mbps =
    (c->stats.delivered_count * c->send_buffer_len * 8) / (1024 * 1024);

  LOG_INFO("%d msgs/sec, %ld Mbps", c->stats.delivered_count, mbps);
  //  LOG_INFO("Client: %d value/sec, %d Mbps, latency min %ld us max %ld "
  //           "us avg %ld us\n",
  //           c->stats.delivered_count, mbps, c->stats.min_latency,
  //           c->stats.max_latency, c->stats.avg_latency);
  memset(&c->stats, 0, sizeof(struct stats));
  mod_timer(&c->stats_ev, jiffies + timeval_to_jiffies(&c->stats_interval));

  do_gettimeofday(&now);
  check_timeout(&now);
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
    return;
  }

  c->clients_timeval = pmalloc(sizeof(struct timeval) * nclients);
  memset(c->clients_timeval, 0, sizeof(struct timeval) * nclients);
  memset(&c->stats, 0, sizeof(struct stats));
  memcpy(c->proposeradd, evpaxos_proposer_address(conf, proposer_id), ETH_ALEN);
  evpaxos_config_free(conf);

  c->val = (struct client_value*)c->send_buffer;
  c->val->size = value_size;
  c->send_buffer_len = sizeof(struct client_value) + value_size;
  random_string(c->val->value, value_size);

  setup_timer(&c->stats_ev, on_stats, (unsigned long)c);
  c->stats_interval = (struct timeval){ 1, 0 };
  mod_timer(&c->stats_ev, jiffies + timeval_to_jiffies(&c->stats_interval));

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
  if (c != NULL) {
    if (c->learner) {
      del_timer(&c->stats_ev);
      evlearner_free(c->learner);
    }
    pfree(c);
  }
  LOG_INFO("Module unloaded.");
}

module_init(init_client);
module_exit(client_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");
