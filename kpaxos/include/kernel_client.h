#ifndef KERNEL_CLIENT
#define KERNEL_CLIENT
#ifdef user_space
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#endif
struct client_value
{
  int            client_id;
  struct timeval t;
  size_t         size;
  char           value[0];
};

struct user_msg
{
  size_t size;
  char   value[0];
};

struct stats
{
  long   min_latency;
  long   max_latency;
  long   avg_latency;
  int    delivered_count;
  size_t delivered_bytes;
};

#endif
