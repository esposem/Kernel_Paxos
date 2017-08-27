#include <linux/module.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/udp.h>
#include <asm/atomic.h>
#include <linux/time.h>
#include <net/sock.h>

#include "paxos.h"
#include "evpaxos.h"
#include "kernel_udp.h"
#include "kernel_device.h"

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
	struct stats stats;
	struct timer_list stats_ev;
	struct timeval stats_interval;
	struct evlearner* learner;
};

struct file_operations fops =
{
  .open = kdev_open,
  .read = kdev_read,
  .write = kdev_write,
  .release = kdev_release,
};

static int isaclient = 0;
module_param(isaclient, int, S_IRUGO);
MODULE_PARM_DESC(isaclient,"If it's a client, set 1");

static udp_service * klearner;
static struct evlearner* lea = NULL;
struct timeval sk_timeout_timeval;
static struct client * c;
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
deliver(unsigned iid, char* value, size_t size, void* arg)
{
	struct client_value* val = (struct client_value*)value;
	printk(KERN_INFO "%s:Learner: %ld.%06ld [%.16s] %ld bytes", klearner->name, val->t.tv_sec, val->t.tv_usec, val->value, (long)val->size);
}

static void
on_deliver(unsigned iid, char* value, size_t size, void* arg)
{
	struct client* c = arg;
	struct client_value* v = (struct client_value*)value;

	if (v->client_id == *clid) {
		update_stats(&c->stats, v, size);
    if(iid % 50000 == 0){
      // printk(KERN_INFO "client %d Called trim, instance %d ", *clid, iid);
      evlearner_send_trim(c->learner, iid);
    }
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
	printk(KERN_INFO "%s %d value/sec, %d Mbps, latency min %ld us max %ld us avg %ld us\n",klearner->name, c->stats.delivered_count, mbps, c->stats.min_latency, c->stats.max_latency, c->stats.avg_latency);
	memset(&c->stats, 0, sizeof(struct stats));
  mod_timer(&c->stats_ev, jiffies + timeval_to_jiffies(&c->stats_interval));
}


static void
start_learner(const char* config)
{
	// struct evlearner* lea;
	if(isaclient){
		c = kmalloc(sizeof(struct client), GFP_KERNEL);
		memset(c, 0, sizeof(struct client));
		c->learner = lea;
		setup_timer( &c->stats_ev,  on_stats, (unsigned long) c);
		c->stats_interval = (struct timeval){1, 0};
	  mod_timer(&c->stats_ev, jiffies + timeval_to_jiffies(&c->stats_interval));
		paxos_config.learner_catch_up = 0;
		lea = evlearner_init(config, on_deliver, c, klearner);
	}else{
		lea = evlearner_init(config, deliver, NULL, klearner);
	}
	if (lea == NULL) {
		// printk(KERN_INFO "%s:Could not start the learner!", klearner->name);
	}else{
		paxos_learner_listen(klearner, lea);
	}
	// printk(KERN_INFO "Called evlearner_free");
	evlearner_free(lea);
}

int udp_server_listen(void)
{
	if(isaclient)
		kdevchar_init(0, "klearner");

  const char* config = "../paxos.conf";
  start_learner(config);
	atomic_set(&klearner->thread_running, 0);
  return 0;
}

void udp_server_start(void){
  klearner->u_thread = kthread_run((void *)udp_server_listen, NULL, klearner->name);
  if(klearner->u_thread >= 0){
    atomic_set(&klearner->thread_running,1);
    // printk(KERN_INFO "%s Thread running [udp_server_start]", klearner->name);
  }else{
    // printk(KERN_INFO "%s Error in starting thread. Terminated [udp_server_start]", klearner->name);
  }
}

static int __init network_server_init(void)
{
	//  ;
  klearner = kmalloc(sizeof(udp_service), GFP_KERNEL);
  if(!klearner){
    // printk(KERN_INFO "Failed to initialize server [network_server_init]");
  }else{
    init_service(klearner, "Learner", -1);
    udp_server_start();
  }
  return 0;
}

static void __exit network_server_exit(void)
{
	if(isaclient){
		del_timer(&c->stats_ev);
		kdevchar_exit();
	}
	if(lea != NULL)
		stop_learner_timer(lea);
  udp_server_quit(klearner);
}

module_init(network_server_init)
module_exit(network_server_exit)
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");
