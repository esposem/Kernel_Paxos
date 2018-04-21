#ifndef __STATS_H__
#define __STATS_H__

#define STATS_MAX_COUNT 10000000

void stats_init(void);
void stats_destroy(void);
void stats_add(long latency);
long stats_get_avg(void);
long stats_get_count(void);
void stats_print(void);
void stats_persist(const char* file);

#endif
