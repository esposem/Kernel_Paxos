#include <linux/module.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/udp.h>
#include <asm/atomic.h>
#include <linux/time.h>
#include <linux/random.h>
#include <net/sock.h>
#include <linux/time.h>
#include <linux/fs.h>

#include "paxos.h"
#include "evpaxos.h"
#include "kernel_udp.h"
#include "kernel_client.h"

static int proposer_id = 0;
module_param(proposer_id, int, S_IRUGO);
MODULE_PARM_DESC(proposer_id,"The proposer id, default 0");

static int outstanding = 1;
module_param(outstanding, int, S_IRUGO);
MODULE_PARM_DESC(outstanding,"The client outstanding, default 1");

static int value_size = 64;
module_param(value_size, int, S_IRUGO);
MODULE_PARM_DESC(value_size,"The size of the message, default 64");

static int id = 0;
module_param(id, int, S_IRUGO);
MODULE_PARM_DESC(id,"The client id, default 0");

static udp_service * kclient;
static struct client* c = NULL;
struct timeval sk_timeout_timeval;

struct client
{
	int id;
  struct sockaddr_in proposeradd;
	int value_size;
	int outstanding;
	char* send_buffer;
	struct stats stats;
	struct timer_list stats_ev;
	struct timeval stats_interval;
	struct evlearner* learner;
};

static void
random_string(char *s, const int len)
{
	int i, j;
	static const char alphanum[] =
		"0123456789abcdefghijklmnopqrstuvwxyz";
	for (i = 0; i < len-1; ++i){
    get_random_bytes(&j, (sizeof j));
    s[i] = alphanum[j % (sizeof(alphanum) - 1)];
  }

	s[len-1] = 0;
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
  // // printk(KERN_INFO "%s Sending the value...", kclient->name);
	paxos_submit(get_sock(c->learner), &c->proposeradd, c->send_buffer, size);
	// printk(KERN_INFO "%s submitted PAXOS_CLIENT_VALUE len %zu value %s",kclient->name, v->size, v->value);
}

static long
timeval_diff(struct timeval* t1, struct timeval* t2)
{
	long us;
	us = (t2->tv_sec - t1->tv_sec) * 1000000;
	if (us < 0) return 0;
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
	stats->avg_latency = stats->avg_latency +
		((lat - stats->avg_latency) / stats->delivered_count);
	if (stats->min_latency == 0 || lat < stats->min_latency)
		stats->min_latency = lat;
	if (lat > stats->max_latency)
		stats->max_latency = lat;
}


static void
on_deliver(unsigned iid, char* value, size_t size, void* arg)
{
	struct client* c = arg;
	struct client_value* v = (struct client_value*)value;

	if (v->client_id == c->id) {
		update_stats(&c->stats, v, size);
    if(iid % 50000 == 0){
      // printk(KERN_INFO "client %d Called trim, instance %d ", c->id, iid);
      evlearner_send_trim(c->learner, iid);
    }
    client_submit_value(c);
    // printk(KERN_INFO "%s On deliver iid:%d value:%.16s",kclient->name, iid, v->value );
  	// struct timeval timenow;
  	// do_gettimeofday(&timenow);
  	// kset_message(timenow, v->value, iid);
	}
}

static void
on_stats(unsigned long arg)
{
	struct client* c = (struct client *) arg;
	int mbps = (int)(c->stats.delivered_bytes * 8) / (1024*1024);
	printk(KERN_INFO "%s %d value/sec, %d Mbps, latency min %ld us max %ld us avg %ld us\n",kclient->name, c->stats.delivered_count, mbps, c->stats.min_latency, c->stats.max_latency, c->stats.avg_latency);
	memset(&c->stats, 0, sizeof(struct stats));
  mod_timer(&c->stats_ev, jiffies + timeval_to_jiffies(&c->stats_interval));
}

static struct client*
make_client(const char* config, int proposer_id, int outstanding, int value_size)
{

	c = kmalloc(sizeof(struct client), GFP_KERNEL);

	memset(&c->stats, 0, sizeof(struct stats));
  struct evpaxos_config* conf = evpaxos_config_read(config);
	if (conf == NULL) {
		// printk(KERN_ERR "%s: Failed to read config file %s\n",kclient->name, config);
		return NULL;
	}
  c->proposeradd = evpaxos_proposer_address(conf, proposer_id);

	get_random_bytes(&c->id, sizeof(int));
  // printk(KERN_INFO "%s  myid [%d]", kclient->name, c->id);
	c->value_size = value_size;
	c->outstanding = outstanding;
	c->send_buffer = kmalloc(sizeof(struct client_value) + value_size, GFP_KERNEL);

  setup_timer( &c->stats_ev,  on_stats, (unsigned long) c);
	c->stats_interval = (struct timeval){1, 0};
  mod_timer(&c->stats_ev, jiffies + timeval_to_jiffies(&c->stats_interval));

	paxos_config.learner_catch_up = 0;
	c->learner = evlearner_init(config, on_deliver, c, kclient);
	if (c->learner == NULL) {
		// printk(KERN_ERR "%s Could not start the learner!", kclient->name);
	}else{
		for (int i = 0; i < c->outstanding; ++i)
	    client_submit_value(c);
		paxos_learner_listen(kclient, c->learner);
	}

	return c;
}

static void
client_free(struct client* c)
{
  del_timer(&c->stats_ev);

	kfree(c->send_buffer);
	if (c->learner)
		evlearner_free(c->learner);
	kfree(c);
}

static void
start_client(const char* config, int proposer_id, int outstanding, int value_size)
{
	struct client* client;
	client = make_client(config, proposer_id, outstanding, value_size);
	if(client)
		client_free(client);
}

static int run_client(void)
{
	const char* config = "../paxos.conf";
	start_client(config, proposer_id, outstanding, value_size);
	atomic_set(&kclient->thread_running, 0);
  return 0;
}


static void start_client_thread(void){
  kclient->u_thread = kthread_run((void *)run_client, NULL, kclient->name);
  if(kclient->u_thread >= 0){
    atomic_set(&kclient->thread_running,1);
    // printk(KERN_INFO "%s Thread running", kclient->name);
  }else{
    // printk(KERN_ERR "%s Error in starting thread", kclient->name);
  }
}

static int __init init_client(void)
{
	if(id < 0 || id > 10){
		// printk(KERN_ERR "you must give an id!");
		return 0;
	}
  kclient = kmalloc(sizeof(udp_service), GFP_KERNEL);
  if(!kclient){
    // printk(KERN_ERR "Failed to initialize CLIENT ");
  }else{
    init_service(kclient, "Client" , id);
    start_client_thread();
  }
  return 0;
}

static void __exit client_exit(void)
{
	if(c != NULL)
		del_timer(&c->stats_ev);
  udp_server_quit(kclient);
}

module_init(init_client)
module_exit(client_exit)
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");
