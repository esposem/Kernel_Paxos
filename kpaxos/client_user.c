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

#define PORT 3002
#define PROP_IP "127.0.0.2"
#define BUFFER_LENGTH 3000
#define MAX_VALUE_SIZE 8192

static char receive[BUFFER_LENGTH];
static int isaclient = 0;
static struct client * cl;

enum paxos_message_type
{
	PAXOS_PREPARE,
	PAXOS_PROMISE,
	PAXOS_ACCEPT,
	PAXOS_ACCEPTED,
	PAXOS_PREEMPTED,
	PAXOS_REPEAT,
	PAXOS_TRIM,
	PAXOS_ACCEPTOR_STATE,
	PAXOS_CLIENT_VALUE,
	PAXOS_LEARNER_HI
};

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
	int fd;
  int socket;
  struct sockaddr_in si_other;
	int value_size;
	int outstanding;
	char* send_buffer;
	struct event_base* base;
	struct event* sig;
};

struct user_msg{
  struct timeval timenow;
  char msg[16]; //copy just the first 16 char of 64
  int iid;
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

  printf("Sending packet\n");
  if(cl->socket != -1){
    if (sendto(cl->socket, packer, size2, 0,(struct sockaddr *) &cl->si_other, sizeof(cl->si_other))==-1)
      perror("sendto()");
    else
    printf("Sent packet\n");
  }

  free(packer);
}

static void
handle_sigint(int sig, short ev, void* arg)
{
	struct event_base* base = arg;
	printf("Client: Caught signal %d\n", sig);
	event_base_loopexit(base, NULL);
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
	random_string(v->value, v->size);
	size_t size = sizeof(struct client_value) + v->size;
	udp_send_msg(v, size);
	printf("Client: submitted PAXOS_CLIENT_VALUE %.16s\n", v->value);
}

void unpack_message(char * msg){
  struct user_msg * t = (struct user_msg *) msg;
  printf("time %ld : %ld, iid %d, message %s\n",t->timenow.tv_sec, t->timenow.tv_usec, t->iid, t->msg);
	client_submit_value(cl);
}

void client_listen(){
  int ret;
	while(1){
    ret = read(cl->fd, receive, BUFFER_LENGTH);
		if (ret >= 0){
			struct user_msg * t = (struct user_msg *) receive;
			if(t->timenow.tv_sec == 0 && t->timenow.tv_usec == 0 && t->msg[0] == '\0' && t->iid == -1){
				printf("STOP\n");
				return;
			}
      unpack_message(receive);
    }else{
			//timeout has expired, means nothing has been received. Resend
			// client_submit_value(cl);
		}
  }
}

static struct client*
make_client(const char* config, int proposer_id, int outstanding, int value_size)
{
	struct client* c;
	c = malloc(sizeof(struct client));
	cl = c;
	c->base = event_base_new();

	printf("Client: Making client, connecting to proposer...\n");
  memset((char *) &cl->si_other, 0, sizeof(cl->si_other));
  cl->si_other.sin_family = AF_INET;
  cl->si_other.sin_port = htons(PORT);

  if (inet_aton(PROP_IP, &cl->si_other.sin_addr)==0) {
    fprintf(stderr, "inet_aton() failed\n");
		exit(1);
  }

  c->fd = open("/dev/chardevice/klearner0", O_RDWR);
   if (c->fd < 0){
    perror("Failed to open the device");
		exit(1);
  }

  c->id = rand();
  printf("id is %d\n",c->id );
  if(c->fd >= 0){
    int ret = write(c->fd, (char *) &c->id, sizeof(int));
    if (ret < 0){
      perror("Failed to write the message to the device");
    }else{
      printf("Sent id to learner");
    }
  }

	c->value_size = value_size;
	c->outstanding = outstanding;
	c->send_buffer = malloc(sizeof(struct client_value) + value_size);

	c->sig = evsignal_new(c->base, SIGINT, handle_sigint, c->base);
	evsignal_add(c->sig, NULL);

	if ((c->socket=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1){
		printf("Socket not working");
		exit(1);
	}

	if(isaclient){
		for (int i = 0; i < c->outstanding; ++i)
			client_submit_value(c);
	}

	client_listen();
	return c;
}

static void
start_client(const char* config, int proposer_id, int outstanding, int value_size)
{
	struct client* client;
	client = make_client(config, proposer_id, outstanding, value_size);
	signal(SIGPIPE, SIG_IGN);
  if(client){
    // event_base_dispatch(client->base);
  	client_free(client);
  }
}

static void
client_free(struct client* c)
{
  close(cl->socket);
	free(c->send_buffer);
	event_free(c->sig);
	event_base_free(c->base);
	free(c);
}

static void
usage(const char* name)
{
	printf("Client Usage: %s [path/to/paxos.conf] [-h] [-o] [-v] [-p]\n", name);
	printf("  %-30s%s\n", "-h, --help", "Output this message and exit");
	printf("  %-30s%s\n", "-o, --outstanding #", "Number of outstanding client values");
	printf("  %-30s%s\n", "-v, --value-size #", "Size of client value (in bytes)");
	printf("  %-30s%s\n", "-p, --proposer-id #", "id of the proposer to connect to");
	printf("  %-30s%s\n", "-c, --client #", "if this is a client or just a learner");
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
			isaclient = 1;
		else
			usage(argv[0]);
		i++;
	}

	gettimeofday(&seed, NULL);
	srand(seed.tv_usec);
	start_client(config, proposer_id, outstanding, value_size);

	return 0;
}
