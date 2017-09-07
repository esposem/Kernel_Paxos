#ifndef USER_CLIENT
#define USER_CLIENT

#include "kernel_client.h"
#include <arpa/inet.h>

extern int learner_id;
extern struct client * cl;

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

extern void serialize_int_to_big(unsigned int * n, unsigned char ** buffer);
extern void cp_int_packet(unsigned int * n, unsigned char ** buffer);
extern int check_for_endianness();
extern void udp_send_msg(struct client_value * clv, size_t size);
extern void init_socket(struct client * c);
extern void open_file(struct client * c);
extern void usage(const char* name);


#endif
