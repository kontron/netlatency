/*
 *  (C) Copyright 2014 Kontron Europe GmbH, Saarbruecken
 */
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <stdint.h>
#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "../timer.c"


/*
 * TESTS
 */
static void test_timespec_diff(void)
{
	struct timespec a;
	struct timespec b;
	struct timespec diff;

	a.tv_sec = 0;
	a.tv_nsec = 0;
	b.tv_sec = 0;
	b.tv_nsec = 0;
	timespec_diff(&a, &b, &diff);
	g_assert_cmpint(diff.tv_sec, ==, 0);
	g_assert_cmpint(diff.tv_nsec, ==, 0);

	a.tv_sec = 1;
	a.tv_nsec = 0;
	b.tv_sec = 0;
	b.tv_nsec = 0;
	timespec_diff(&a, &b, &diff);
	g_assert_cmpint(diff.tv_sec, ==, -1);
	g_assert_cmpint(diff.tv_nsec, ==, 0);

	a.tv_sec = 0;
	a.tv_nsec = 0;
	b.tv_sec = 1;
	b.tv_nsec = 0;
	timespec_diff(&a, &b, &diff);
	g_assert_cmpint(diff.tv_sec, ==, 1);
	g_assert_cmpint(diff.tv_nsec, ==, 0);

	a.tv_sec = 0;
	a.tv_nsec = 100000000;
	b.tv_sec = 0;
	b.tv_nsec = 0;
	timespec_diff(&a, &b, &diff);
	g_assert_cmpint(diff.tv_sec, ==, 0);
	g_assert_cmpint(diff.tv_nsec, ==, -100000000);

	a.tv_sec = 0;
	a.tv_nsec = 0;
	b.tv_sec = 0;
	b.tv_nsec = 100000000;
	timespec_diff(&a, &b, &diff);
	g_assert_cmpint(diff.tv_sec, ==, 0);
	g_assert_cmpint(diff.tv_nsec, ==, 100000000);

	a.tv_sec = 0;
	a.tv_nsec = 900000000;
	b.tv_sec = 0;
	b.tv_nsec = 100000000;
	timespec_diff(&a, &b, &diff);
	g_assert_cmpint(diff.tv_sec, ==, 0);
	g_assert_cmpint(diff.tv_nsec, ==, -800000000);

	a.tv_sec = 0;
	a.tv_nsec = 100000000;
	b.tv_sec = 0;
	b.tv_nsec = 900000000;
	timespec_diff(&a, &b, &diff);
	g_assert_cmpint(diff.tv_sec, ==, 0);
	g_assert_cmpint(diff.tv_nsec, ==, 800000000);

	a.tv_sec = 1;
	a.tv_nsec = 100000000;
	b.tv_sec = 0;
	b.tv_nsec = 900000000;
	timespec_diff(&a, &b, &diff);
	g_assert_cmpint(diff.tv_sec, ==, 0);
	g_assert_cmpint(diff.tv_nsec, ==, -200000000);

	a.tv_sec = 0;
	a.tv_nsec = 900000000;
	b.tv_sec = 1;
	b.tv_nsec = 100000000;
	timespec_diff(&a, &b, &diff);
	g_assert_cmpint(diff.tv_sec, ==, 0);
	g_assert_cmpint(diff.tv_nsec, ==, 200000000);

	a.tv_sec = 0;
	a.tv_nsec = 900000000;
	b.tv_sec = 10;
	b.tv_nsec = 100000000;
	timespec_diff(&a, &b, &diff);
	g_assert_cmpint(diff.tv_sec, ==, 9);
	g_assert_cmpint(diff.tv_nsec, ==, 200000000);

	a.tv_sec = 1519657344;
	a.tv_nsec = 900000000;
	b.tv_sec = 1519657344;
	b.tv_nsec = 100000000;
	timespec_diff(&a, &b, &diff);
	g_assert_cmpint(diff.tv_sec, ==, 0);
	g_assert_cmpint(diff.tv_nsec, ==, -800000000);

	a.tv_sec = 1519657344;
	a.tv_nsec = 900000000;
	b.tv_sec = 1519657354;
	b.tv_nsec = 100000000;
	timespec_diff(&a, &b, &diff);
	g_assert_cmpint(diff.tv_sec, ==, 9);
	g_assert_cmpint(diff.tv_nsec, ==, 200000000);

}

static void test_get_timeval_to_next_slice(void)
{
	struct timespec now;
	struct timespec next;
	struct timespec interval;

	now.tv_sec = 0;
	now.tv_nsec = 0;
	interval.tv_sec= 0;
	interval.tv_nsec= 1000000;
	get_timeval_to_next_slice(&now, &next, &interval);
	g_assert_cmpint(next.tv_sec, ==, 0);
	g_assert_cmpint(next.tv_nsec, ==, 1000000);

	now.tv_sec = 0;
	now.tv_nsec = 990000;
	interval.tv_sec = 0;
	interval.tv_nsec = 1000000;
	get_timeval_to_next_slice(&now, &next, &interval);
	g_assert_cmpint(next.tv_sec, ==, 0);
	g_assert_cmpint(next.tv_nsec, ==, 1000000);

	now.tv_sec = 0;
	now.tv_nsec = 999000000;
	interval.tv_sec = 0;
	interval.tv_nsec = 1000000;
	get_timeval_to_next_slice(&now, &next, &interval);
	g_assert_cmpint(next.tv_sec, ==, 1);
	g_assert_cmpint(next.tv_nsec, ==, 0);

	now.tv_sec = 0;
	now.tv_nsec = 0;
	interval.tv_sec = 0;
	interval.tv_nsec = 1000000000;
	get_timeval_to_next_slice(&now, &next, &interval);
	g_assert_cmpint(next.tv_sec, ==, 1);
	g_assert_cmpint(next.tv_nsec, ==, 0);

	now.tv_sec = 0;
	now.tv_nsec = 500000000;
	interval.tv_sec = 0;
	interval.tv_nsec = 1000000000;
	get_timeval_to_next_slice(&now, &next, &interval);
	g_assert_cmpint(next.tv_sec, ==, 1);
	g_assert_cmpint(next.tv_nsec, ==, 0);

	now.tv_sec = 0;
	now.tv_nsec = 999030000;
	interval.tv_sec = 0;
	interval.tv_nsec = 1000000;
	get_timeval_to_next_slice(&now, &next, &interval);
	g_assert_cmpint(next.tv_sec, ==, 1);
	g_assert_cmpint(next.tv_nsec, ==, 0);
}

static void test_timespec_to_iso_string(void)
{
    struct timespec t;
    char *s;

    t.tv_sec = 0;
    t.tv_nsec = 0;
    s = timespec_to_iso_string(&t);
    g_assert(s != NULL);
    g_assert_cmpstr(s, ==, "1970-01-01T00:00:00.000000000Z");
    g_free(s);

    t.tv_sec = 0;
    t.tv_nsec = 5000000;
    s = timespec_to_iso_string(&t);
    g_assert(s != NULL);
    g_assert_cmpstr(s, ==, "1970-01-01T00:00:00.005000000Z");
    g_free(s);

    t.tv_sec = 1520944655;
    t.tv_nsec = 5000000;
    s = timespec_to_iso_string(&t);
    g_assert(s != NULL);
    g_assert_cmpstr(s, ==, "2018-03-13T12:37:35.005000000Z");
    g_free(s);
}

#if 0
void test_a_less_b(void)
{
	struct timespec a;
	struct timespec b;

	a.tv_sec = 0;
	a.tv_nsec = 0;
	b.tv_sec = 0;
	b.tv_nsec = 0;
	g_assert_false(a_less_b(&a, &b));

	a.tv_sec = 1;
	a.tv_nsec = 0;
	b.tv_sec = 0;
	b.tv_nsec = 0;
	g_assert_false(a_less_b(&a, &b));

	a.tv_sec = 0;
	a.tv_nsec = 124;
	b.tv_sec = 0;
	b.tv_nsec = 0;
	g_assert_false(a_less_b(&a, &b));

	a.tv_sec = 100;
	a.tv_nsec = 124;
	b.tv_sec = 100;
	b.tv_nsec = 0;
	g_assert_false(a_less_b(&a, &b));

	a.tv_sec = 0;
	a.tv_nsec = 0;
	b.tv_sec = 1;
	b.tv_nsec = 0;
	g_assert_true(a_less_b(&a, &b));

	a.tv_sec = 1;
	a.tv_nsec = 0;
	b.tv_sec = 1;
	b.tv_nsec = 12211321;
	g_assert_true(a_less_b(&a, &b));

	a.tv_sec = 1;
	a.tv_nsec = 124;
	b.tv_sec = 3;
	b.tv_nsec = 0;
	g_assert_true(a_less_b(&a, &b));
}
#endif

#if 0
void test_get_timer_before_target(void)
{
	struct timespec now;
	struct timespec ts_target;
	struct itimerspec timer;

	now.tv_sec = 1;
	now.tv_nsec = 0;
	ts_target.tv_sec = 1;
	ts_target.tv_nsec = 200000;
	get_timer_before_target(200000, &now, &ts_target, &timer);
	g_assert_cmpint(timer.it_value.tv_sec, ==, -1);
	g_assert_cmpint(timer.it_value.tv_nsec, ==, -1);

	now.tv_sec = 1;
	now.tv_nsec = 0;
	ts_target.tv_sec = 2;
	ts_target.tv_nsec = 100000;
	get_timer_before_target(200000, &now, &ts_target, &timer);
	g_assert_cmpint(timer.it_value.tv_sec, ==, 1);
	g_assert_cmpint(timer.it_value.tv_nsec, ==, 999900000);

	now.tv_sec = 0;
	now.tv_nsec = 0;
	ts_target.tv_sec = 1;
	ts_target.tv_nsec = 0;
	get_timer_before_target(200000, &now, &ts_target, &timer);
	g_assert_cmpint(timer.it_value.tv_sec, ==, 0);
	g_assert_cmpint(timer.it_value.tv_nsec, ==, 999800000);

	now.tv_sec = 0;
	now.tv_nsec = 0;
	ts_target.tv_sec = 0;
	ts_target.tv_nsec = 900000000;
	get_timer_before_target(200000, &now, &ts_target, &timer);
	g_assert_cmpint(timer.it_value.tv_sec, ==, 0);
	g_assert_cmpint(timer.it_value.tv_nsec, ==, 899800000);

	now.tv_sec = 1519037653;
	now.tv_nsec = 999993435;
	ts_target.tv_sec = 1519037654;
	ts_target.tv_nsec = 0;
	get_timer_before_target(300000, &now, &ts_target, &timer);
	g_assert_cmpint(timer.it_value.tv_sec, ==, -1);
	g_assert_cmpint(timer.it_value.tv_nsec, ==, -1);

	now.tv_sec = 1519039623;
	now.tv_nsec = 999058882;
	ts_target.tv_sec = 1519039624;
	ts_target.tv_nsec = 0;
	get_timer_before_target(300000, &now, &ts_target, &timer);
	g_assert_cmpint(timer.it_value.tv_sec, ==, 1519039623);
	g_assert_cmpint(timer.it_value.tv_nsec, ==, 999700000);

}
#endif

int main(int argc, char** argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/timer/test_timespec_diff",
			test_timespec_diff);

	g_test_add_func("/timer/get_timeval_to_next_slice/valid",
			test_get_timeval_to_next_slice);

	g_test_add_func("/timer/timespec_to_iso_string/valid",
			test_timespec_to_iso_string);

#if 0
	g_test_add_func("/timer/a_less_b/false",
			test_a_less_b);
#endif

#if 0
	g_test_add_func("/timer/get_time_before_target",
			test_get_timer_before_target);
#endif


	return g_test_run();
}
