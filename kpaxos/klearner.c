#include <linux/module.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/udp.h>
#include <asm/atomic.h>
#include <linux/time.h>
#include <net/sock.h>

#define USE_CLIENT 0
#define LCHAR_OP 0

#include "paxos.h"
#include "evpaxos.h"
#include "kernel_udp.h"
#include "kernel_client.h"

#if LCHAR_OP
	#include "kernel_device.h"
	struct file_operations fops =
	{
	  .open = kdev_open,
	  .read = kdev_read,
	  .write = kdev_write,
	  .release = kdev_release,
	};
#endif

#if USE_CLIENT
	struct client
	{
		struct stats stats;
		struct timer_list stats_ev;
		struct timeval stats_interval;
		struct evlearner* learner;
	};
#endif

static int cantrim = 0;
module_param(cantrim, int, S_IRUGO);
MODULE_PARM_DESC(cantrim,"If it's a client, set 1");

static int id = 0;
module_param(id, int, S_IRUGO);
MODULE_PARM_DESC(id,"The learner id, default 0");

static udp_service * klearner;
static struct evlearner* lea = NULL;
struct timeval sk_timeout_timeval;

static atomic_t rcv;
// static struct timeval interval;
// static struct timer_list receive_nothing;
// static int flag = 0;

#if USE_CLIENT

	static struct client * cl;

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
	on_stats(unsigned long arg)
	{
		struct client* c = (struct client *) arg;
		int mbps = (int)(c->stats.delivered_bytes * 8) / (1024*1024);
		printk(KERN_INFO "%s %d value/sec, %d Mbps, latency min %ld us max %ld us avg %ld us\n",klearner->name, c->stats.delivered_count, mbps, c->stats.min_latency, c->stats.max_latency, c->stats.avg_latency);
		memset(&c->stats, 0, sizeof(struct stats));
	  mod_timer(&c->stats_ev, jiffies + timeval_to_jiffies(&c->stats_interval));
	}
#endif

static void
deliver(unsigned iid, char* value, size_t size, void* arg)
{
	atomic_inc(&rcv);
	struct client_value* val = (struct client_value*)value;
	printk(KERN_INFO "%s %d [%.16s] %ld bytes", klearner->name, iid, val->value, (long)val->size);
	// printk(KERN_INFO "%s %ld.%06ld [%.16s] %ld bytes", klearner->name, val->t.tv_sec, val->t.tv_usec, val->value, (long)val->size);
}

static void
on_deliver(unsigned iid, char* value, size_t size, void* arg)
{
	atomic_inc(&rcv);
	struct client_value* v = (struct client_value*)value;
	printk(KERN_INFO "%s %d [%.16s] %ld bytes", klearner->name, iid, v->value, (long)v->size);

	// if(flag == 0)
		// mod_timer(&receive_nothing, jiffies + timeval_to_jiffies(&interval));
		#if USE_CLIENT
			struct client* c = arg;
			update_stats(&c->stats, v, size);
		#endif

    if(iid % 100000 == 0){
      printk(KERN_ALERT "Called trim, instance %d ", iid);
      evlearner_send_trim(lea, iid);
    }
    // printk(KERN_INFO "%s On deliver iid:%d value:%.16s",klearner->name, iid, v->value );
		#if LCHAR_OP
  	 	kset_message(&v->t, v->value, v->client_id, iid);
		#endif
	// 	if(flag){
	// 		printk(KERN_INFO "%s Received something", klearner->name);
	// 	}
}

// static void change_flag(unsigned long arg){
// 	printk(KERN_INFO "%s Received nothing for a second", klearner->name);
// 	printk(KERN_INFO "%s Received nothing for a second", klearner->name);
// 	flag = 1;
// }


static void
start_learner(const char* config)
{
	if(cantrim){
		paxos_config.learner_catch_up = 0;

		#if USE_CLIENT
			struct client * c = kmalloc(sizeof(struct client), GFP_KERNEL);
			cl = c;
			memset(c, 0, sizeof(struct client));
			setup_timer( &c->stats_ev,  on_stats, (unsigned long) c);
			c->stats_interval = (struct timeval){1, 0};
	  	mod_timer(&c->stats_ev, jiffies + timeval_to_jiffies(&c->stats_interval));
			c->learner = lea;
			lea = evlearner_init(config, on_deliver, c, klearner);
		#else
			lea = evlearner_init(config, on_deliver, NULL, klearner);
		#endif

		// setup_timer( &receive_nothing,  change_flag, 0);
		// interval = (struct timeval) {1,0};
		// struct timeval interval2 = (struct timeval) {3,0};
		// mod_timer(&receive_nothing, jiffies + timeval_to_jiffies(&interval2));

	}else{
		lea = evlearner_init(config, deliver, NULL, klearner);
	}

	if (lea == NULL) {
		// printk(KERN_ERR "%s:Could not start the learner!", klearner->name);
	}else{
		paxos_learner_listen(klearner, lea);
		evlearner_free(lea);
	}
}

static int run_learner(void)
{
	if(id < 0 || id > 10){
		// printk(KERN_ERR "you must give an id!");
		return 0;
	}

	#if LCHAR_OP
		kdevchar_init(id, "klearner");
	#endif
	atomic_set(&rcv,0);
  const char* config = "../paxos.conf";
  start_learner(config);
	atomic_set(&klearner->thread_running, 0);
  return 0;
}

static void start_learner_thread(void){
  klearner->u_thread = kthread_run((void *)run_learner, NULL, klearner->name);
  if(klearner->u_thread >= 0){
    atomic_set(&klearner->thread_running,1);
    printk(KERN_INFO "%s Thread running", klearner->name);
  }else{
    // printk(KERN_ERR "%s Error in starting thread", klearner->name);
  }
}

static int __init init_learner(void)
{
  klearner = kmalloc(sizeof(udp_service), GFP_KERNEL);
  if(!klearner){
    // printk(KERN_ERR "Failed to initialize server");
  }else{
    init_service(klearner, "Learner", id);
    start_learner_thread();
  }
  return 0;
}

static void __exit learner_exit(void)
{
	if(cantrim){
		#if USE_CLIENT
		del_timer(&cl->stats_ev);
		#endif
		// del_timer(&receive_nothing);
	}

	#if LCHAR_OP
		kstop_device();
		kdevchar_exit();
	#endif
	printk(KERN_ERR "Received %d", atomic_read(&rcv));
  udp_server_quit(klearner);
}

module_init(init_learner)
module_exit(learner_exit)
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");
