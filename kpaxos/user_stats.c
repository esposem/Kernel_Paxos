#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "user_levent.h"
#include "user_stats.h"

#define TIMEOUT_US 1000000

static long
timeval_diff(struct timeval* t1, struct timeval* t2)
{
  long us;
  us = (t2->tv_sec - t1->tv_sec) * 1e6;

  if (us < 0)
    return 0;
  us += (t2->tv_usec - t1->tv_usec);
  return us;
}

static void
check_timeout(struct client* client)
{
  struct timeval now;
  gettimeofday(&now, NULL);
  for (int i = 0; i < client->nclients; ++i) {
    if (timeval_diff(&client->nclients_time[i], &now) > TIMEOUT_US) {
      printf("Client %d sent expired %ld\n", i,
             timeval_diff(&client->nclients_time[i], &now));
      client_submit_value(client, i + client->id);
    }
  }
}

void
on_stats(evutil_socket_t fd, short event, void* arg)
{
  struct client* c = arg;
  double         mbps = (double)(c->stats.delivered_bytes * 8) / (1024 * 1024);
  printf("Client: %d value/sec, %.2f Mbps\n", c->stats.delivered_count, mbps);
  memset(&c->stats, 0, sizeof(struct stats));
  check_timeout(c);
  event_add(c->stats_ev, &c->stats_interval);
}

void
update_stats(struct stats* stats, struct timeval delivered, size_t size)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  long lat = timeval_diff(&delivered, &tv);
  stats->delivered_count++;
  stats->delivered_bytes += size;
  stats->avg_latency =
    stats->avg_latency + ((lat - stats->avg_latency) / stats->delivered_count);
  if (stats->min_latency == 0 || lat < stats->min_latency)
    stats->min_latency = lat;
  if (lat > stats->max_latency)
    stats->max_latency = lat;
}
