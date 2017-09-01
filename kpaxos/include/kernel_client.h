#ifndef KERNEL_CLIENT
#define KERNEL_CLIENT

struct client_value
{
	int client_id;
	struct timeval t;
	size_t size;
	char value[0];
};

struct user_msg{
  struct timeval timenow;
	int client_id;
  char msg[64];
  int iid;
};

struct stats
{
	long min_latency;
	long max_latency;
	long avg_latency;
	int delivered_count;
	size_t delivered_bytes;
};

#endif
