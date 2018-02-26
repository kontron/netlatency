#ifndef __TIMER_H__
#define __TIMER_H__

void timespec_diff(const struct timespec *a, const struct timespec *b,
                   struct timespec *result);

int timer_init(void);

void nanosleep_until(struct timespec *ts, int delay);

void wait_for_next_timeslice(int interval_ms, struct timespec *ts_desired);

#endif /* __TIMER_H__ */
