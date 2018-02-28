#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include "stats.h"
#include "timer.h"

#define TIME_ARRAY_SIZE 100

struct stamps {
	int index;
	int count;
	struct timespec tsa[TIME_ARRAY_SIZE];
};

struct stamps ts_array = {
	.index = -1,
	.count = 0,
};

struct stamps diffs_array = {
	.index = -1,
	.count = 0,
};

void add_stamps(struct timespec *ts, struct stamps *sa)
{
	if (sa->index == -1) {
		sa->index = 0;
	}

	memcpy(&sa->tsa[sa->index], ts, sizeof(struct timespec));

	if (sa->count < TIME_ARRAY_SIZE) {
		sa->count++;
	}

	if (sa->index == (TIME_ARRAY_SIZE - 1)) {
		sa->index = 0;
	} else {
		sa->index++;
	}
}

struct timespec calc_rms(struct stamps *sa)
{
	int i;
	long long sum = 0;
	struct timespec mean;
	for (i = 0; i < sa->count; i++) {
		sum += (sa->tsa[i].tv_nsec * sa->tsa[i].tv_nsec);
	}
	memset(&mean, 0, sizeof(struct timespec));
	mean.tv_nsec = sqrt(sum / sa->count);
	return mean;
}

void get_diff_to_index(struct stamps *sa, struct timespec *ts,
		int index_offset, struct timespec *ts_diff)
{
	(void)index_offset;

	if (sa->count > 2) {
		int offset = -1;

		if (sa->index == 0) {
			offset = TIME_ARRAY_SIZE - 1 - 1;
		} else if (sa->index == 1) {
			offset = TIME_ARRAY_SIZE - 1;
		} else {
			offset = sa->index - 1 - 1;
		}
		timespec_diff(&sa->tsa[offset], ts, ts_diff);
	} else {
		memset(ts_diff, 0, sizeof(struct timespec));
	}
}

void check_for_max(struct timespec *val, struct stats *stats)
{

	int m = labs(val->tv_nsec);
	if (m > stats->max.tv_nsec) {
		stats->max.tv_nsec = m;
		stats->max.tv_sec = val->tv_sec;
	}
}

void calc_stats(struct timespec *ts, struct stats *stats,
		struct timespec *interval)
{
	add_stamps(ts, &ts_array);

	if (ts_array.count > 2) {
		get_diff_to_index(&ts_array, ts, -1, &stats->diff);
		stats->diff.tv_nsec -= (interval->tv_nsec);
		add_stamps(&stats->diff, &diffs_array);

		stats->mean = calc_rms(&diffs_array);
		check_for_max(&stats->diff, stats);
	}
}
