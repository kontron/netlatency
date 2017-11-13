#ifndef __STATS_H__
#define __STATS_H__

#include <time.h>
struct stats {
	struct timespec diff;
	struct timespec mean;
	struct timespec min;
	struct timespec max;
};

void calc_stats(struct timespec *ts, struct stats *stats, int interval_us);

void timespec_diff(struct timespec *start, struct timespec *stop,
		struct timespec *result);
#endif
