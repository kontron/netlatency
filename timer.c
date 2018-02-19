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


#define TIME_BEFORE_NS 300000

int get_timeval_to_next_slice(struct timespec *now, struct timespec *next,
        int interval_ms)
{
	gint64 interval_ns = (gint64)interval_ms * 1000000;

	if ((now->tv_nsec + interval_ns) >= 1000000000) {
		next->tv_sec = now->tv_sec + 1;
		next->tv_nsec = 0;

	} else {
		next->tv_sec = now->tv_sec;
		next->tv_nsec =
			((now->tv_nsec / interval_ns) + 1) * interval_ns;
	}

	return 0;
}

static void get_time_before_target(int time_before_ns, struct timespec *now,
		struct timespec *ts_target, struct itimerspec *timer_before)
{
	memset(timer_before, 0, sizeof(struct itimerspec));

	timer_before->it_value.tv_sec = ts_target->tv_sec;
	timer_before->it_value.tv_nsec = ts_target->tv_nsec;

	/* jump over second */
	if (ts_target->tv_sec > now->tv_sec) {
		timer_before->it_value.tv_nsec =
				(1000000000 - time_before_ns + ts_target->tv_nsec);
	} else {
		timer_before->it_value.tv_nsec -= time_before_ns;
	}
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

static void busy_poll_to_target_time(struct timespec *ts_target)
{
	struct timespec now;
	int loopcount = 0;

	if (clock_gettime(CLOCK_REALTIME, &now)) {
		perror("clock_gettime");
	}

	while (a_less_b(&now, ts_target)) {
		if (clock_gettime(CLOCK_REALTIME, &now)) {
			perror("clock_gettime");
		}
		loopcount++;
	}
	printf("loop %d\n", loopcount);
}

void wait_for_next_timeslice(int interval_ms, struct timespec *ts_desired)
{
	struct timespec ts_now;
	struct timespec ts_end;
	struct timespec ts_before_wait;
	struct timespec ts_target;


	if (clock_gettime(CLOCK_REALTIME, &ts_now)) {
		perror("clock_gettime");
	}

	/* calculate wanted target time */
	get_timeval_to_next_slice(&ts_now, &ts_target, interval_ms);
	if (ts_desired != NULL) {
		memcpy(ts_desired, &ts_target, sizeof(struct timespec));
	}


	/* set estimated time before and start timer */
	{
		struct itimerspec timer_before;
		int timer_fd;
		ssize_t s;

		get_time_before_target(TIME_BEFORE_NS, &ts_now, &ts_target, &timer_before);

		timer_fd = timerfd_create(CLOCK_REALTIME, 0);
		if (timer_fd == -1) {
			perror("timerfd_create");
		}

		if (timerfd_settime(timer_fd, TFD_TIMER_ABSTIME, &timer_before, NULL)) {

			printf("timer: %lld.%.06ld\n",
				(long long)timer_before.it_value.tv_sec,
				(timer_before.it_value.tv_nsec / 1000)
			);
			perror("timerfd_settime");
		}

		/* wait for timer */
		uint64_t exp;
		s = read(timer_fd, &exp, sizeof(exp));
		if (s != sizeof(s)) {
			perror("read");
		}

		close(timer_fd);
	}

	if (clock_gettime(CLOCK_REALTIME, &ts_before_wait)) {
		perror("clock_gettime");
	}

	busy_poll_to_target_time(&ts_target);

	if (clock_gettime(CLOCK_REALTIME, &ts_end)) {
		perror("clock_gettime");
	}
}
