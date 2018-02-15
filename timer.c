#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/timerfd.h>

#include <glib.h>

void nanosleep_until(struct timespec *ts, int delay)
{
	ts->tv_nsec += delay;
	/* check for next second */
	if (ts->tv_nsec >= 1000000000) {
		ts->tv_nsec -= 1000000000;
		ts->tv_sec++;
	}
	clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, ts, NULL);
}


#define TIMESLICE_INTERVAL_MS 10000000
#define TIME_BEFORE_NS 300000

int get_timeval_to_next_slice(struct timespec *now, struct timespec *next,
        int interval_ms)
{
	gint64 interval_ns = interval_ms * 1000000;
	if ((now->tv_nsec + interval_ns) > 1000000000) {
		//printf("jump to next second");
		next->tv_sec = now->tv_sec + 1;
		next->tv_nsec = 0;
	} else {
		next->tv_sec = now->tv_sec;
		next->tv_nsec =
			((now->tv_nsec / interval_ns) + 1) * interval_ns;
	}

	return 0;
}

static inline int a_less_b(const struct timespec *a, const struct timespec *b)
{
	if (a->tv_sec == b->tv_sec) {
		return a->tv_nsec < b->tv_nsec;
	} else {
		return a->tv_sec < b->tv_sec;
	}
	return 0;
}

void wait_for_next_timeslice(int interval_ms, struct timespec *ts_desired)
{
	int timer_fd;
	struct timespec ts_start;
	struct timespec ts_end;
	struct timespec ts_before_wait;
	struct timespec ts_target;
	struct itimerspec new_value;
	ssize_t s;

	timer_fd = timerfd_create(CLOCK_REALTIME, 0);
	if (timer_fd == -1) {
		perror("timerfd_create");
	}

	if (clock_gettime(CLOCK_REALTIME, &ts_start)) {
		perror("clock_gettime");
	}

	/* calculate wanted target time */
	get_timeval_to_next_slice(&ts_start, &ts_target, interval_ms);
	if (ts_desired != NULL) {
		memcpy(ts_desired, &ts_target, sizeof(struct timespec));
	}

	/* set estimated time before  */
	memset(&new_value, 0, sizeof(new_value));

	if (ts_target.tv_nsec == 0) {

		new_value.it_value.tv_sec = ts_target.tv_sec - 1;
		new_value.it_value.tv_nsec = ts_start.tv_nsec + 10;

	} else {

		if (ts_target.tv_nsec > TIME_BEFORE_NS) {
			new_value.it_value.tv_nsec = ts_target.tv_nsec - TIME_BEFORE_NS;
		} else {
			new_value.it_value.tv_nsec = ts_start.tv_nsec + 10;
		}
		new_value.it_value.tv_sec = ts_target.tv_sec;
	}

	if (timerfd_settime(timer_fd, TFD_TIMER_ABSTIME, &new_value, NULL)) {

		printf("timer: %lld.%.06ld\n",
			(long long)new_value.it_value.tv_sec,
			(new_value.it_value.tv_nsec / 1000)
		);
		perror("timerfd_settime");
	}

	/* wait for timer */
	uint64_t exp;
	s = read(timer_fd, &exp, sizeof(exp));
	if (s != sizeof(s)) {
		perror("read");
	}

	if (clock_gettime(CLOCK_REALTIME, &ts_before_wait)) {
		perror("clock_gettime");
	}

	int loopcount = 0;

	/* do busy polling until target time */
	{
		struct timespec now;
		if (clock_gettime(CLOCK_REALTIME, &now)) {
			perror("clock_gettime");
		}

		while (a_less_b(&now, &ts_target)) {
			if (clock_gettime(CLOCK_REALTIME, &now)) {
				perror("clock_gettime");
			}
			loopcount ++;
		}
	}


	if (clock_gettime(CLOCK_REALTIME, &ts_end)) {
		perror("clock_gettime");
	}

	close(timer_fd);
}
