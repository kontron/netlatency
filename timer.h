#ifndef __TIMER_H__
#define __TIMER_H__

//void timeout_info(int signo);
//void timer_init_sigaction(void);
int timer_init(void);

void nanosleep_until(struct timespec *ts, int delay);

void wait_for_next_timeslice(int interval_ms);

#endif /* __TIMER_H__ */
