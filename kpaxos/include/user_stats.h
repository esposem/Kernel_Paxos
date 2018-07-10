#ifndef USER_STATS
#define USER_STATS

#include "kernel_client.h"
#include "stats.h"

#define TIMEOUT_US 1000000

extern void update_stats(struct stats* stats, struct timeval delivered,
                         size_t size);
extern void on_stats(struct client* cl);
extern long timeval_diff(struct timeval* t1, struct timeval* t2);
extern void check_timeout(struct client* client);
#endif
