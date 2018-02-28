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

#define NSEC_PER_SEC 1000000000
void timespec_diff(struct timespec *a, struct timespec *b,
                   struct timespec *result)
{
	gint64 diff;
	diff = NSEC_PER_SEC * (gint64)((gint64) b->tv_sec - (gint64) a->tv_sec);;
	diff += ((gint64) b->tv_nsec - (gint64) a->tv_nsec);
	result->tv_sec = diff / NSEC_PER_SEC;
	result->tv_nsec = diff % NSEC_PER_SEC;
}

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
        struct timespec *interval)
{
	gint64 interval_ns = interval->tv_nsec;

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

static void get_timer_before_target(int time_before_ns, struct timespec *now,
		struct timespec *ts_target, struct itimerspec *timer_before)
{
	memset(timer_before, 0, sizeof(struct itimerspec));

	timer_before->it_value.tv_sec = ts_target->tv_sec;
	timer_before->it_value.tv_nsec = ts_target->tv_nsec;

//	printf("------\n");

	gint64 diff = 0;

	diff = ts_target->tv_nsec - now->tv_nsec;

	if (ts_target->tv_sec > now->tv_sec) {
		diff += 1000000000;
//		printf("diff a ....%ld \n", diff);
	}
//	printf("diff b ....%ld \n", diff);


	if (diff <= time_before_ns) {
//		printf("x ....%ld\n", diff);
		timer_before->it_value.tv_sec = -1;
		timer_before->it_value.tv_nsec = -1;
		return;
	}

	if (ts_target->tv_sec > now->tv_sec) {
//		printf("a ....\n");
		if (1000000000 - now->tv_nsec <= time_before_ns) {
			timer_before->it_value.tv_nsec = 0;
			printf("a.1 ....\n");
		} else if (ts_target->tv_nsec < time_before_ns) {
//			printf("a.2 ....\n");
			timer_before->it_value.tv_sec = ts_target->tv_sec - 1;
			timer_before->it_value.tv_nsec = 1000000000 - time_before_ns + ts_target->tv_nsec;

		} else {
//			printf("a.3 ....\n");
			timer_before->it_value.tv_nsec -= time_before_ns;
		}
	} else {
//		printf("b ....\n");
		timer_before->it_value.tv_sec = ts_target->tv_sec;
		timer_before->it_value.tv_nsec = ts_target->tv_nsec - time_before_ns;
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
}

void wait_for_next_timeslice(struct timespec *interval,
		struct timespec *ts_desired)
{
	struct timespec ts_now;
	struct timespec ts_end;
	struct timespec ts_before_wait;
	struct timespec ts_target;


	if (clock_gettime(CLOCK_REALTIME, &ts_now)) {
		perror("clock_gettime");
	}

	/* calculate wanted target time */
	get_timeval_to_next_slice(&ts_now, &ts_target, interval);
	if (ts_desired != NULL) {
		memcpy(ts_desired, &ts_target, sizeof(struct timespec));
	}


	struct itimerspec timer_before;
	get_timer_before_target(TIME_BEFORE_NS, &ts_now, &ts_target,
			&timer_before);
	/* set estimated time before and start timer */
	if (timer_before.it_value.tv_sec != -1) {
		int timer_fd;
		ssize_t s;

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
