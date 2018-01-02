#ifndef __TIMER_H__
#define __TIMER_H__

int timer_init(void);

void nanosleep_until(struct timespec *ts, int delay);

void wait_for_next_timeslice(int interval_ms);

#endif /* __TIMER_H__ */
