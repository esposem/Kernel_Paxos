#ifndef USER_STATS
#define USER_STATS

#include "kernel_client.h"

extern void update_stats(struct stats* stats, struct timeval delivered, size_t size);
extern void on_stats(evutil_socket_t fd, short event, void *arg);

#endif
