#include <linux/module.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/udp.h>
#include <asm/atomic.h>
#include <linux/time.h>
#include <linux/random.h>
#include <net/sock.h>
#include <linux/time.h>

#include "paxos.h"
#include "evpaxos.h"

#include "kernel_udp.h"

#define MAX_VALUE_SIZE 8192

struct client_value
{
	int client_id;
	struct timeval t;
	size_t size;
	char value[0];
};

struct stats
{
	long min_latency;
	long max_latency;
	long avg_latency;
	int delivered_count;
	size_t delivered_bytes;
};

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

static udp_service * kclient;
struct client* c = NULL;

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
	paxos_submit(get_sock(c->learner), &c->proposeradd, c->send_buffer, size);
	printk(KERN_INFO "Client: submitted PAXOS_CLIENT_VALUE len %zu value %s",v->size, v->value);
}

// Returns t2 - t1 in microseconds.
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
		client_submit_value(c);
	}
	printk(KERN_INFO "Client: On deliver iid:%d value:%.16s",iid, v->value );
}

static void
on_stats(unsigned long arg)
{
	struct client* c = (struct client *) arg;
	int mbps = (int)(c->stats.delivered_bytes * 8) / (1024*1024);
	printk(KERN_INFO "%s: %d value/sec, %d Mbps, latency min %ld us max %ld us avg %ld us\n",
		kclient->name, c->stats.delivered_count, mbps, c->stats.min_latency,
		c->stats.max_latency, c->stats.avg_latency);
	memset(&c->stats, 0, sizeof(struct stats));
  mod_timer(&c->stats_ev, jiffies + timeval_to_jiffies(&c->stats_interval));
}

static struct client*
make_client(const char* config, int proposer_id, int outstanding, int value_size)
{

	c = kmalloc(sizeof(struct client), GFP_KERNEL);

	memset(&c->stats, 0, sizeof(struct stats));
	printk(KERN_INFO "Client: Making client, connecting to proposer...");
  struct evpaxos_config* conf = evpaxos_config_read(config);
	if (conf == NULL) {
		printk(KERN_INFO "%s: Failed to read config file %s\n",kclient->name, config);
		return NULL;
	}
  c->proposeradd = evpaxos_proposer_address(conf, proposer_id);

	get_random_bytes(&c->id, sizeof(int));
	c->value_size = value_size;
	c->outstanding = outstanding;
	c->send_buffer = kmalloc(sizeof(struct client_value) + value_size, GFP_KERNEL);

  setup_timer( &c->stats_ev,  on_stats, (unsigned long) c);
	c->stats_interval = (struct timeval){1, 0};
  mod_timer(&c->stats_ev, jiffies + timeval_to_jiffies(&c->stats_interval));

	paxos_config.learner_catch_up = 0;
	printk(KERN_INFO "Client: Creating an internal learner...");

	c->learner = evlearner_init(config, on_deliver, c, kclient);
	if (c->learner == NULL) {
		printk(KERN_INFO "%s:Could not start the learner!", kclient->name);
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
	client_free(client);
}

int udp_server_listen(void)
{
	int proposer_id = 0; //TODO param
	int outstanding = 1; //TODO param
	int value_size = 64; //TODO param
	const char* config = "../paxos.conf";
	start_client(config, proposer_id, outstanding, value_size);
	atomic_set(&kclient->thread_running, 0);
  return 0;
}

void udp_server_start(void){
  kclient->u_thread = kthread_run((void *)udp_server_listen, NULL, kclient->name);
  if(kclient->u_thread >= 0){
    atomic_set(&kclient->thread_running,1);
    printk(KERN_INFO "%s Thread running [udp_server_start]", kclient->name);
  }else{
    printk(KERN_INFO "%s Error in starting thread. Terminated [udp_server_start]", kclient->name);
  }
}

static int __init network_server_init(void)
{
  kclient = kmalloc(sizeof(udp_service), GFP_KERNEL);
  if(!kclient){
    printk(KERN_INFO "Failed to initialize CLIENT [network_server_init]");
  }else{
    init_service(kclient, "Client" , -1);
    udp_server_start();
  }
  return 0;
}

static void __exit network_server_exit(void)
{
  // clent_free() should have been freed when it's stopped
	if(c != NULL)
		del_timer(&c->stats_ev);
  udp_server_quit(kclient);
}

module_init(network_server_init)
module_exit(network_server_exit)
MODULE_LICENSE("MIT");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");
