#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "user_levent.h"
#include "user_stats.h"

long
timeval_diff(struct timeval* t1, struct timeval* t2)
{
  long us;
  us = (t2->tv_sec - t1->tv_sec) * 1e6;

  if (us < 0)
    return 0;
  us += (t2->tv_usec - t1->tv_usec);
  return us;
}

void
check_timeout(struct client* client)
{
  struct timeval now;
  gettimeofday(&now, NULL);
  for (int i = 0; i < client->nclients; ++i) {
    long diff = timeval_diff(&client->nclients_time[i], &now);
    if (diff > TIMEOUT_US) {
      printf("Client %d sent expired %ld\n", i + client->id, diff);
      client_submit_value(client, i + client->id);
    }
  }
}

void
on_stats(struct client* c)
{
  double mbps =
    (double)(c->stats.delivered_count * c->send_buffer_len * 8) / (1024 * 1024);
  printf("Client: %d value/sec, %.2f Mbps\n", c->stats.delivered_count, mbps);
  memset(&c->stats, 0, sizeof(struct stats));
  check_timeout(c);
}

void
update_stats(struct stats* stats, struct timeval delivered, size_t size)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  // long lat = timeval_diff(&delivered, &tv);
  stats->delivered_count++;
  // stats->delivered_bytes += size;
  // stats->avg_latency =
  //   stats->avg_latency + ((lat - stats->avg_latency) /
  //   stats->delivered_count);
  // if (stats->min_latency == 0 || lat < stats->min_latency)
  //   stats->min_latency = lat;
  // if (lat > stats->max_latency)
  //   stats->max_latency = lat;
}

static long*           a = 0; // absolute time
static long*           e = 0; // elapsed time between calls to stats_add
static unsigned long   count;
static struct timespec then;

void
stats_init()
{
  e = malloc(STATS_MAX_COUNT * sizeof(long));
  memset(e, 0, STATS_MAX_COUNT * sizeof(long));
  a = malloc(STATS_MAX_COUNT * sizeof(long));
  memset(a, 0, STATS_MAX_COUNT * sizeof(long));
  count = 0;
  clock_gettime(CLOCK_MONOTONIC_RAW, &then);
}

void
stats_destroy()
{
  count = 0;
  if (a)
    free(a);
  if (e)
    free(e);
}

void
stats_add(long latency)
{
  struct timespec now;

  if (count == STATS_MAX_COUNT) {
    printf("Stats error: array is full\n");
    return;
  }

  clock_gettime(CLOCK_MONOTONIC_RAW, &now);
  a[count] = (now.tv_sec - then.tv_sec) * 1000000 +
             (now.tv_nsec - then.tv_nsec) / 1000000;
  e[count++] = latency;
  then = now;
}

long
stats_get_avg()
{
  int  i;
  long avg = 0;

  for (i = 0; i < count; ++i)
    avg += (e[i] - avg) / (i + 1);

  return avg;
}

long
stats_get_count()
{
  return count;
}

void
stats_print()
{
  printf("Statistics with %lu entries average of %ldus\n", count,
         stats_get_avg());
}

void
stats_persist(const char* file)
{
  int  i;
  long abs = 0;

  FILE* f = fopen(file, "w+");
  char  line[128];

  if (f) {
    sprintf(line, "#ORDER\tLATENCY\tABS\n");
    fwrite(line, 1, strlen(line), f);
    for (i = 0; i < count; i++) {
      abs += a[i];
      sprintf(line, "%d\t%ld\t%ld\n", (i + 1), e[i], abs);
      fwrite(line, 1, strlen(line), f);
    }
    sync();
    fclose(f);
  } else {
    printf("Stats: could not save statistics to file '%s'\n", file);
  }
}
