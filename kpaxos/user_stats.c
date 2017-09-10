#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "user_levent.h"
#include "user_stats.h"

void on_stats(evutil_socket_t fd, short event, void *arg){
	struct client* c = arg;
	double mbps = (double)(c->stats.delivered_bytes * 8) / (1024*1024);
	printf("Client: %d value/sec, %.2f Mbps, latency min %ld us max %ld us avg %ld us\n", c->stats.delivered_count, mbps, c->stats.min_latency, c->stats.max_latency, c->stats.avg_latency);
	memset(&c->stats, 0, sizeof(struct stats));
	event_add(c->stats_ev, &c->stats_interval);
}

static long timeval_diff(struct timeval* t1, struct timeval* t2){
	long us;
	us = (t2->tv_sec - t1->tv_sec) * 1e6;

	if (us < 0) return 0;
	us += (t2->tv_usec - t1->tv_usec);
	return us;
}

void update_stats(struct stats* stats, struct timeval delivered, size_t size){
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
