#include "stats.h"
#include "common.h"
#include "kfile.h"
#include "paxos.h"
#include <linux/time.h>
#include <linux/vmalloc.h>
#include <paxos.h>

static long*          a = 0; // absolute time
static long*          e = 0; // elapsed time between calls to stats_add
static unsigned long  count;
static struct timeval then;

void
stats_init()
{
  e = vmalloc(STATS_MAX_COUNT * sizeof(long));
  memset(e, 0, STATS_MAX_COUNT * sizeof(long));
  a = vmalloc(STATS_MAX_COUNT * sizeof(long));
  memset(a, 0, STATS_MAX_COUNT * sizeof(long));
  count = 0;
  do_gettimeofday(&then);
}

void
stats_destroy()
{
  count = 0;
  if (a)
    vfree(a);
  if (e)
    vfree(e);
}

void
stats_add(long latency)
{
  struct timeval now;
  long           us;

  if (count == STATS_MAX_COUNT) {
    paxos_log_error("Stats error: array is full");
    return;
  }

  do_gettimeofday(&now);
  us = (now.tv_sec - then.tv_sec) * 1000000;
  if (us < 0)
    us = 0;

  us += (now.tv_usec - then.tv_usec);
  a[count] = us;
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
  paxos_log_info("Statistics with %d entries average of %.2fus", count,
                 stats_get_avg());
}

void
stats_persist(const char* file)
{
  int                i;
  long               abs = 0;
  unsigned long long offset = 0;
  struct file*       f = file_open(file, O_CREAT | O_WRONLY | O_TRUNC, 00666);
  char               line[128];

  if (f) {
    sprintf(line, "#ORDER\tLATENCY\tABS\n");
    offset += file_write(f, offset, line, strlen(line));
    for (i = 0; i < count; i++) {
      abs += a[i];
      sprintf(line, "%d\t%ld\t%ld\n", (i + 1), e[i], abs);
      offset += file_write(f, offset, line, strlen(line));
    }
    file_sync(f);
    file_close(f);
  } else {
    paxos_log_error("Stats: could not save statistics to file '%s'", file);
  }
}
