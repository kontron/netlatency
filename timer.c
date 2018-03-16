/*
* Copyright (c) 2018, Kontron Europe GmbH
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice, this
*    list of conditions and the following disclaimer.
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include <errno.h>
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

void wait_for_next_timeslice(struct timespec *interval,
		struct timespec *next)
{
	int rc;
	struct timespec ts_now;
	struct timespec ts_target;

	if (clock_gettime(CLOCK_REALTIME, &ts_now)) {
		perror("clock_gettime");
	}

	/* calculate wanted target time */
	get_timeval_to_next_slice(&ts_now, &ts_target, interval);
	if (next != NULL) {
		memcpy(next, &ts_target, sizeof(struct timespec));
	}

	rc = clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts_target, NULL);
	if (rc != 0) {
		if (rc != EINTR) {
			perror("clock_nanosleep failed");
		}
	}

	return;
}

char *timespec_to_iso_string(struct timespec *time)
{
    GString *iso_string;
    gchar *s;
    GTimeVal t;

    t.tv_sec = time->tv_sec;
    t.tv_usec = 0; // let it zero .. append nsec later
    s = g_time_val_to_iso8601(&t);

    /* remove trailing 'Z' */
    iso_string = g_string_new_len(s, strlen(s)-1);
    g_free(s);

    g_string_append_printf(iso_string, ".%09ld", time->tv_nsec);
    g_string_append_printf(iso_string, "Z");

    return g_string_free(iso_string, FALSE);
}
