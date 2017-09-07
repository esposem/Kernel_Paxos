#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <event2/event.h>
#include <stdatomic.h>
#include <unistd.h>
#include "user.h"

#define BUFFER_LENGTH 1000

static char receive[BUFFER_LENGTH];
struct client * cl;
static int use_chardevice = 0, cansend = 0, use_socket = 0;
static atomic_int sent = 0, sent_sec = 0;
int learner_id = -1;
static void client_free(struct client* c);

static void
handle_sigint(int sig, short ev, void* arg)
{
	struct event_base* base = arg;
	printf("Total sent %d\n", sent);
	printf("Client: Caught signal %d\n", sig);
	event_base_loopbreak(base);
}

static void
random_string(char *s, const int len)
{
	int i;
	static const char alphanum[] =
		"0123456789abcdefghijklmnopqrstuvwxyz";
	for (i = 0; i < len-1; ++i)
		s[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
	s[len-1] = 0;
}


static void
client_submit_value(struct client* c)
{
	struct client_value* v = (struct client_value*)c->send_buffer;
	v->client_id = c->id;
	gettimeofday(&v->t, NULL);
	v->size = c->value_size;

	/* ############################################
	   HERE YOU SET THE VALUE TO SEND, YOU HAVE V->SIZE BYTES*/
	random_string(v->value, v->size);
	/* ############################################ */

	size_t size = sizeof(struct client_value) + v->size;
	udp_send_msg(v, size);
	// printf("Client: submitted PAXOS_CLIENT_VALUE %.16s\n", v->value);
	sent++;
	sent_sec++;
}

static long
timeval_diff(struct timeval* t1, struct timeval* t2)
{
	long us;
	us = (t2->tv_sec - t1->tv_sec) * 1e6;

	if (us < 0) return 0;
	us += (t2->tv_usec - t1->tv_usec);
	return us;
}

static void
update_stats(struct stats* stats, struct timeval delivered, size_t size)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	long lat = timeval_diff(&delivered, &tv);
	stats->delivered_count++;
	stats->delivered_bytes += size;
	stats->avg_latency = stats->avg_latency +
		((lat - stats->avg_latency) / stats->delivered_count);
	if (stats->min_latency == 0 || lat < stats->min_latency)
		stats->min_latency = lat;
	if (lat > stats->max_latency)
		stats->max_latency = lat;
}

void unpack_message(char * msg){
  struct user_msg * mess = (struct user_msg *) msg;
	struct client_value * val = (struct client_value *) mess->value;
	// printf("On deliver iid:%d value:%.16s\n",mess->iid, val->value );

	if(cansend && val->client_id == cl->id){
		update_stats(&cl->stats, val->t, cl->value_size);
		event_add(cl->resend_ev, &cl->reset_interval);
		struct timespec t, j;
		t.tv_sec = 0;
		t.tv_nsec = 10000; //0.01ms

		// nanosleep(&t, &j);
		client_submit_value(cl);
	}else if (!cansend){
		printf("On deliver iid:%d value:%.16s\n",mess->iid, val->value );
	}


}

static void client_read(evutil_socket_t fd, short event, void *arg) {
  int len;
  struct event *ev = arg;
  len = read(cl->fd, receive, BUFFER_LENGTH);

  if (len < 0) {
    if (len == -2){
			printf("Stopped by kernel module\n");
      event_del(ev);
      event_base_loopbreak(event_get_base(ev));
    }
    return;
  }
  unpack_message(receive);
}

static void
on_resend(evutil_socket_t fd, short event, void *arg)
{
	client_submit_value(cl);
	event_add(cl->resend_ev, &cl->reset_interval);
}

static void
on_stats(evutil_socket_t fd, short event, void *arg)
{
	struct client* c = arg;
	double mbps = (double)(c->stats.delivered_bytes * 8) / (1024*1024);
	sent_sec = 0;
	printf("Client: %d value/sec, %.2f Mbps, latency min %ld us max %ld us avg %ld us\n", c->stats.delivered_count, mbps, c->stats.min_latency, c->stats.max_latency, c->stats.avg_latency);
	memset(&c->stats, 0, sizeof(struct stats));
	event_add(c->stats_ev, &c->stats_interval);
}


static void
make_client(int proposer_id, int outstanding, int value_size)
{
	struct client* c;
	c = malloc(sizeof(struct client));
	if(c == NULL){
		return;
	}
	cl = c;

	struct event_config *cfg = event_config_new();
  event_config_avoid_method(cfg, "epoll");
  c->base = event_base_new_with_config(cfg);

	c->id = rand();
	printf("id is %d\n",c->id );

	if(use_chardevice){
		open_file(c);
		size_t s = sizeof(struct client_value) + value_size;
	  if(c->fd >= 0){
	    int ret = write(c->fd, (char *) &s, sizeof(size_t));
	    if (ret < 0){
	      perror("Failed to write the message to the device");
	    }
	  }

		c->evread = event_new(c->base, c->fd, EV_READ | EV_PERSIST, client_read, event_self_cbarg());
		event_add(c->evread, NULL);
	}


	c->value_size = value_size;

	c->sig = evsignal_new(c->base, SIGINT, handle_sigint, c->base);
	evsignal_add(c->sig, NULL);

	if(cansend){
		init_socket(c);
		// print statistic every 1 sec
		c->stats_interval = (struct timeval){1, 0};
		c->reset_interval = (struct timeval){1, 0};
		c->stats_ev = evtimer_new(c->base, on_stats, c);
		event_add(c->stats_ev, &c->stats_interval);

		// resend value after 1 sec I did not receive anything
		c->resend_ev = evtimer_new(c->base, on_resend, NULL);
		event_add(c->resend_ev, &c->reset_interval);
		client_submit_value(c);
	}

	event_base_dispatch(c->base);
	client_free(c);
	event_config_free(cfg);
}

static void
start_client(int proposer_id, int outstanding, int value_size)
{
	make_client(proposer_id, outstanding, value_size);
	// signal(SIGPIPE, SIG_IGN);
	libevent_global_shutdown();
}

static void
client_free(struct client* c)
{
	event_free(c->sig);
	if(use_chardevice){
		event_free(c->evread);
	}
	if(cansend){
		free(c->send_buffer);
		event_free(c->stats_ev);
		event_free(c->resend_ev);
		close(c->socket);
	}
	event_base_free(c->base);
	free(c);
}

int
main(int argc, char const *argv[])
{
	int i = 1;
	int proposer_id = 0;
	int outstanding = 1;
	int value_size = 64;
	struct timeval seed;
	int idcall = 0;

	while (i != argc) {
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
			usage(argv[0]);
		else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--outstanding") == 0)
			outstanding = atoi(argv[++i]);
		else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--value-size") == 0)
			value_size = atoi(argv[++i]);
		else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--proposer-id") == 0)
			proposer_id = atoi(argv[++i]);
		else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--client") == 0)
			cansend = 1;
		else if (strcmp(argv[i], "-id") == 0){
			idcall = 1;
			learner_id = atoi(argv[++i]);
		}
		else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--device") == 0 )
			use_chardevice = 1;
		else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--socket") == 0 )
			use_socket = 1;
		else
			usage(argv[0]);
		i++;
	}

	if(use_chardevice == 1 && use_socket == 1 ){
		printf("Either use chardevice or connect remotely to a listener\n");
		exit(0);
	}

	if(use_chardevice == 1 && idcall == 0 ){
		printf("If you want to use chardevice, use both -id and -d\n");
		exit(0);
	}

	gettimeofday(&seed, NULL);
	srand(seed.tv_usec);
	start_client(proposer_id, outstanding, value_size);

	return 0;
}
