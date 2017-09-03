#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <event2/event.h>
#include <stdatomic.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

#include "paxos_types.h"
#include "kernel_client.h"

#define PORT 3002
#define PROP_IP "127.0.0.2"
#define BUFFER_LENGTH 1000

#define CCHAR_OP 0
#define RESEND_TIMER 0

static char receive[BUFFER_LENGTH];
static int cansend = 0;
static struct client * cl;
static int learner_id = -1;
static atomic_int sent = 0;
static atomic_int sent_sec = 0;

struct client
{
	int id;
	int fd;
  int socket;
	int value_size;
	char* send_buffer;
	struct stats stats;
	struct event *evread;
	struct event* stats_ev;
	struct event* resend_ev;
	struct event_base* base;
	struct sockaddr_in si_other;
	struct timeval stats_interval;
	struct timeval reset_interval;
	struct event* sig;
};

static void client_free(struct client* c);

void serialize_int_to_big(unsigned int * n, unsigned char ** buffer){
	(*buffer)[0] = *n >> 24;
  (*buffer)[1] = *n >> 16;
  (*buffer)[2] = *n >> 8;
  (*buffer)[3] = *n;
  *buffer += sizeof(unsigned int);
}

void cp_int_packet(unsigned int * n, unsigned char ** buffer){
	memcpy(*buffer, n, sizeof(unsigned int));
	*buffer+=sizeof(unsigned int);
}

// returns 1 if architecture is little endian, 0 in case of big endian.
int check_for_endianness()
{
  unsigned int x = 1;
  char *c = (char*) &x;
  return (int)*c;
}

void udp_send_msg(struct client_value * clv, size_t size)
{
  unsigned char * packer;
  unsigned int len = size;
  long size2 = (sizeof(unsigned int) * 2) + len;
  packer = malloc(size2);
  unsigned char * tmp = packer;
  unsigned int type = PAXOS_CLIENT_VALUE;
  char * value = (char *)clv;

  if(check_for_endianness()){
    // Machine is little endian, transform the packet data from little
		// to big endian
    serialize_int_to_big(&type, &tmp);
    serialize_int_to_big(&len, &tmp);
  }else{
    cp_int_packet(&type, &tmp);
    cp_int_packet(&len, &tmp);
  }
  memcpy(tmp, value, len);

  if(cl->socket != -1){
    if (sendto(cl->socket, packer, size2, 0,(struct sockaddr *) &cl->si_other, sizeof(cl->si_other))==-1)
      perror("sendto()");
  }

  free(packer);
}

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
update_stats(struct stats* stats, struct timeval* delivered, size_t size)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	long lat = timeval_diff(delivered, &tv);
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
  struct user_msg * t = (struct user_msg *) msg;
	// printf("my id %d, its id %d\n", cl->id, t->client_id );
	if(t->client_id == cl->id){
		update_stats(&cl->stats, &t->timenow, cl->value_size);
		// printf("Client: On deliver iid:%d value:%.16s\n",t->iid, t->msg );
		client_submit_value(cl);
	}
	#if RESEND_TIMER
	event_add(cl->resend_ev, &cl->reset_interval);
	#endif
}

#if CCHAR_OP
	static void client_read(evutil_socket_t fd, short event, void *arg) {
	  int len;
	  struct event *ev = arg;
		// printf("Calling read...\n");
	  len = read(cl->fd, receive, BUFFER_LENGTH);
		// printf("Read called\n");

	  if (len < 0) {
	    if (len == -2){
				printf("Stopped by kernel module\n");
	      event_del(ev);
	      event_base_loopbreak(event_get_base(ev));
	    }
	    return;
	  }
		// printf("Received something\n");
	  unpack_message(receive);
	}
#endif

static void
on_resend(evutil_socket_t fd, short event, void *arg)
{
	// printf("Client did not receive anything for one second!\n");
	client_submit_value(cl);
	#if RESEND_TIMER
	event_add(cl->resend_ev, &cl->reset_interval);
	#endif
}

static void
on_stats(evutil_socket_t fd, short event, void *arg)
{
	struct client* c = arg;
	double mbps = (double)(c->stats.delivered_bytes * 8) / (1024*1024);
	printf("Sent %d \n", sent_sec);
	sent_sec = 0;
	// printf("Client: %d value/sec, %.2f Mbps, latency min %ld us max %ld us avg %ld us\n", c->stats.delivered_count, mbps, c->stats.min_latency, c->stats.max_latency, c->stats.avg_latency);
	memset(&c->stats, 0, sizeof(struct stats));
	event_add(c->stats_ev, &c->stats_interval);
}

void
make_client(const char* config, int proposer_id, int outstanding, int value_size)
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

  memset((char *) &cl->si_other, 0, sizeof(cl->si_other));
  cl->si_other.sin_family = AF_INET;
  cl->si_other.sin_port = htons(PORT);

  if (inet_aton(PROP_IP, &cl->si_other.sin_addr)==0) {
    fprintf(stderr, "inet_aton() failed\n");
		free(c);
		exit(1);
  }

	#if CCHAR_OP
	if(learner_id < 0){
		printf("Error: insert learner id > 0\n");
		free(c);
		exit(1);
	}


	char * def_name = "/dev/chardevice/klearner0";
	size_t strl = strlen(def_name) + 1;
	char * filename = malloc(strl);
	memcpy(filename, def_name, strl);
	filename[strl-2] = learner_id + '0';
  c->fd = open(filename, O_RDWR | O_NONBLOCK, 0);
   if (c->fd < 0){
    perror("Failed to open the device");
		free(c);
		exit(1);
  }
	#endif

	c->id = rand();
  // c->id = learner_id;
  printf("id is %d\n",c->id );

	#if CCHAR_OP
	  if(c->fd >= 0){
	    int ret = write(c->fd, (char *) &c->value_size, sizeof(int));
	    if (ret < 0){
	      perror("Failed to write the message to the device");
	    }
	  }

		c->evread = event_new(c->base, c->fd, EV_READ | EV_PERSIST, client_read,
													event_self_cbarg());

		event_add(c->evread, NULL);
	#endif

	c->value_size = value_size;

	c->sig = evsignal_new(c->base, SIGINT, handle_sigint, c->base);
	evsignal_add(c->sig, NULL);

	if(cansend){
		c->send_buffer = malloc(sizeof(struct client_value) + value_size);

		if ((c->socket=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1){
			printf("Socket not working\n");
			free(c);
			free(c->send_buffer);
			exit(1);
		}

		struct sockaddr_in si_me;
		memset((char *) &si_me, 0, sizeof(si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = 0;
		inet_aton("127.0.0.1", &si_me.sin_addr);
    if (bind(c->socket, (struct sockaddr *)&si_me, sizeof(si_me))==-1)
      perror("bind");
		struct sockaddr_in address;
		int i = (int) sizeof(struct sockaddr_in);
    getsockname(c->socket, (struct sockaddr *) &address, &i);
    printf("Socket is bind to %s : %d \n", inet_ntoa(address.sin_addr), ntohs(address.sin_port));
		// print statistic every 1 sec
		c->stats_interval = (struct timeval){1, 0};
		c->reset_interval = (struct timeval){0, 1};
		c->stats_ev = evtimer_new(c->base, on_stats, c);
		event_add(c->stats_ev, &c->stats_interval);

		// resend value after 1 sec I did not receive anything
		#if RESEND_TIMER
		c->resend_ev = evtimer_new(c->base, on_resend, NULL);
		event_add(c->resend_ev, &c->reset_interval);
		#else
		client_submit_value(c);
		#endif
	}



	event_base_dispatch(c->base);
	client_free(c);
	event_config_free(cfg);
}

static void
start_client(const char* config, int proposer_id, int outstanding, int value_size)
{
	make_client(config, proposer_id, outstanding, value_size);
	// signal(SIGPIPE, SIG_IGN);
	libevent_global_shutdown();
}

static void
client_free(struct client* c)
{
	event_free(c->sig);
	#if CCHAR_OP
		event_free(c->evread);
		// close(c->fd);
	#endif
	if(cansend){
		free(c->send_buffer);
		event_free(c->stats_ev);
		#if RESEND_TIMER
		event_free(c->resend_ev);
		#endif
		close(c->socket);
	}
	event_base_free(c->base);
	free(c);
}

static void
usage(const char* name)
{
	printf("Client Usage: %s [path/to/paxos.conf] [-h] [-o] [-v] [-p] [-c] [-id]\n", name);
	printf("  %-30s%s\n", "-h, --help", "Output this message and exit");
	printf("  %-30s%s\n", "-o, --outstanding #", "Number of outstanding client values");
	printf("  %-30s%s\n", "-v, --value-size #", "Size of client value (in bytes)");
	printf("  %-30s%s\n", "-p, --proposer-id #", "id of the proposer to connect to");
	printf("  %-30s%s\n", "-c, --client #", "if this is a client or just a learner");
	printf("  %-30s%s\n", "-id #", "the learner module id");
	exit(1);
}

int
main(int argc, char const *argv[])
{
	int i = 1;
	int proposer_id = 0;
	int outstanding = 1;
	int value_size = 64;
	struct timeval seed;
	const char* config = "../paxos.conf";

	if (argc > 1 && argv[1][0] != '-') {
		config = argv[1];
		i++;
	}

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
		else if (strcmp(argv[i], "-id") == 0)
			learner_id = atoi(argv[++i]);
		else
			usage(argv[0]);
		i++;
	}

	gettimeofday(&seed, NULL);
	srand(seed.tv_usec);
	start_client(config, proposer_id, outstanding, value_size);

	return 0;
}
