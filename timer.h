#ifndef __TIMER_H__
#define __TIMER_H__

void timespec_diff(const struct timespec *a, const struct timespec *b,
                   struct timespec *result);

int timer_init(void);

void nanosleep_until(struct timespec *ts, int delay);

int get_timeval_to_next_slice(struct timespec *now, struct timespec *next,
        struct timespec *interval);

void wait_for_next_timeslice(struct timespec *interval,
		struct timespec *ts_desired);

void wait_for_next_timeslice_legacy(struct timespec *interval,
		struct timespec *next);

char *timespec_to_iso_string(struct timespec *time);

#endif /* __TIMER_H__ */
