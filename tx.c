/*
 * Copyright (c) 2000-2006 Dug Song <dugsong@monkey.org>
 * All rights reserved, all wrongs reversed.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * 3. The names of the authors and copyright holders may not be used
 *    to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/ioctl.h>
#include <net/if.h>
#include <netpacket/packet.h>
//#include <net/ethernet.h>
#include <netinet/in.h>
#include <netinet/ether.h>
#include <linux/sockios.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <inttypes.h>

#include <linux/errqueue.h>

#include <glib.h>
#include <glib/gprintf.h>

#include "config_control.h"
#include "data.h"
#include "stats.h"
#include "timer.h"
#include "timestamps.h"

static gchar *help_description = NULL;
static gint o_verbose = 0;
static gint o_quiet = 0;
static gint o_version = 0;
static gint o_run_timer = 0;
static gint o_run_thread = 0;
static gchar *o_destination_mac = "FF:FF:FF:FF:FF:FF";
static gint o_sched_prio = -1;
static gint o_memlock = 1;

gint o_interval_us = 0;
gint o_packet_size = -1;
gboolean o_pause_loop = FALSE;

static uint8_t buf[2048];
struct ether_testpacket *tp = (struct ether_testpacket*)buf;

struct eth_handle {
	int fd;
	struct ifreq ifr;
	struct sockaddr_ll sll;
};

typedef struct eth_handle eth_t;

eth_t *eth_close(eth_t *e)
{
	if (e != NULL) {
		if (e->fd >= 0) {
			close(e->fd);
		}
		free(e);
	}
	return NULL;
}

eth_t *eth_open(const char *device)
{
	eth_t *e;

	if ((e = calloc(1, sizeof(*e))) != NULL) {
		if ((e->fd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) < 0) {
			return (eth_close(e));
		}

		strncpy(e->ifr.ifr_name, device, sizeof(e->ifr.ifr_name));
		/* terminate string with 0 */
		e->ifr.ifr_name[sizeof(e->ifr.ifr_name)-1] = 0;

		if (ioctl(e->fd, SIOCGIFINDEX, &e->ifr) < 0) {
			return eth_close(e);
		}

		e->sll.sll_family = AF_PACKET;
		e->sll.sll_ifindex = e->ifr.ifr_ifindex;
	}

	return e;
}

ssize_t eth_send(eth_t *e, const void *buf, size_t len)
{
	struct ether_header *eth = (struct ether_header *)buf;

	e->sll.sll_protocol = eth->ether_type;

	return (sendto(e->fd, buf, len, 0, (struct sockaddr *)&e->sll,
			sizeof(e->sll)));
}

void usage(void)
{
	g_printf("%s", help_description);
}

static GOptionEntry entries[] = {
	{ "destination", 'd', 0, G_OPTION_ARG_STRING,
			&o_destination_mac, "Destination MAC address", NULL },
	{ "interval",    'i', 0, G_OPTION_ARG_INT,
			&o_interval_us, "Interval in micro seconds", NULL },
	{ "timer",       't', 0, G_OPTION_ARG_NONE,
			&o_run_timer, "Run timer", NULL },
	{ "thread",      'r', 0, G_OPTION_ARG_NONE,
			&o_run_thread, "Run in thread", NULL },
	{ "prio",        'p', 0, G_OPTION_ARG_INT,
			&o_sched_prio, "Set scheduler priority", NULL },
	{ "memlock",     'm', 0, G_OPTION_ARG_INT,
			&o_memlock, "Configure memlock (default is 1)", NULL },
	{ "padding",     'P', 0, G_OPTION_ARG_INT,
			&o_packet_size, "Set the packet size", NULL },
	{ "verbose",     'v', 0, G_OPTION_ARG_NONE,
			&o_verbose, "Be verbose", NULL },
	{ "quiet",       'q', 0, G_OPTION_ARG_NONE,
			&o_quiet, "Suppress error messages", NULL },
	{ "version",     'V', 0, G_OPTION_ARG_NONE,
			&o_version, "Show version inforamtion and exit", NULL },
	{ NULL, 0, 0, 0, NULL, NULL, NULL }
};

gint parse_command_line_options(gint *argc, char **argv)
{
	GError *error = NULL;
	GOptionContext *context;

	context = g_option_context_new("DEVICE - receive timestamped packets");

	g_option_context_add_main_entries(context, entries, NULL);
	g_option_context_set_description(context,
		"description tbd\n"
	);

	if (!g_option_context_parse(context, argc, &argv, &error)) {
		g_print("option parsing failed: %s\n", error->message);
		exit(1);
	}

	help_description = g_option_context_get_help(context, 0, NULL);
	g_option_context_free(context);

	return 0;
}

static void config_thread(void)
{
	int rc;
	struct sched_param sp = {0};
	int policy = SCHED_FIFO;
	int max_prio;

	/* Lock memory, prevent memory from being paged to the swap area */
	if (o_verbose) {
		printf("config memlock %d\n", o_memlock);
	}
	if (o_memlock) {
		if(mlockall(MCL_CURRENT|MCL_FUTURE) == -1) {
			printf("mlockall failed: %m\n");
			exit(-2);
		}
	}

	if (o_verbose) {
		printf("SCHED_FIFO min %d / max %d\n",
				sched_get_priority_min(SCHED_FIFO),
				sched_get_priority_max(SCHED_FIFO));
		printf("SCHED_RR min %d / max %d\n",
				sched_get_priority_min(SCHED_RR),
				sched_get_priority_max(SCHED_RR));
	}

	max_prio = sched_get_priority_max(policy);
	if (o_sched_prio == -1) {
		o_sched_prio = max_prio;
	}

	sp.sched_priority = o_sched_prio;
	rc = pthread_setschedparam(pthread_self(), policy, &sp);
	//rc = pthread_setschedparam(pthread_self(), SCHED_RR, &sp);
	if (rc) {
		perror("pthread_setschedparam()");
		exit (1);
	}

	if (o_verbose) {
		struct sched_param sp;
		int policy;
		pthread_getschedparam(pthread_self(), &policy, &sp);
		printf("scheduler\n");
		switch (policy) {
			case SCHED_FIFO:
				printf("  policy: SCHED_FIFO\n");
				break;
			case SCHED_RR:
				printf("  policy: SCHED_RR\n");
				break;
			case SCHED_OTHER:
				printf("  policy: SCHED_OTHER\n");
				break;
			default:
				printf("  policy: ???\n");
				break;
		}
		printf("  priority: %d\n", sp.sched_priority);
	}
}

#if 0
static void timer_handler(int signum)
{
	struct timespec ts;

	(void)signum;

	clock_gettime(CLOCK_REALTIME, &ts);


	printf("%lld.%.3ld.%3ld.%3ld\n",
		(long long)ts.tv_sec,
		ts.tv_nsec / 1000000,
		(ts.tv_nsec / 1000)%1000,
		ts.tv_nsec %1000
	);
}
#endif

void busy_poll(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	while (((ts.tv_nsec / 1000) % 1000) != 0) {
		clock_gettime(CLOCK_REALTIME, &ts);
	}

}

#if 0
void run_by_timer(void)
{
	timer_t t_id;
	struct itimerspec tim_spec = {
				.it_interval = { .tv_sec = 0, .tv_nsec = 0 },
				.it_value = { .tv_sec = 1, .tv_nsec = 0 }
	};
	tim_spec.it_interval.tv_nsec = o_interval_us * 1000;
	struct sigaction action;
	sigset_t set;

	printf("starting timer loop\n");

	sigemptyset(&set);
	sigaddset(&set, SIGALRM);

	action.sa_flags = 0;
	action.sa_mask = set;
	action.sa_handler = &timer_handler;

	sigaction(SIGALRM, &action, NULL);

	if (timer_create(CLOCK_MONOTONIC, NULL, &t_id)) {
		perror("timer_create()");
	}

	if (timer_settime(t_id, 0, &tim_spec, NULL)) {
		perror("timer_settime()");
	}

	while(1);
}
#endif

#if 0
void * thread_code(void)
{
	pthread_make_periodic_np(pthread_self(), gethrtime(), 1000000000);

	while (1) {
		pthread_wait_np();
		rtl_printf("Hello World\n");
	}

	return 0;
}
#endif

void *thread_func(void *data)
{
	struct timespec sleep_ts;
	struct timespec ts;
	/* Do RT specific stuff here */
	(void)data;
	clock_gettime(CLOCK_MONOTONIC, &sleep_ts);

	struct stats stats;

	memset(&stats, 0, sizeof(struct stats));


	while (1) {
		clock_gettime(CLOCK_REALTIME, &ts);

		calc_stats(&ts, &stats, o_interval_us);

#if 0
		printf("NOW   %lld.%.03ld.%03ld",
			(long long)ts.tv_sec,
			ts.tv_nsec / 1000000,
			(ts.tv_nsec / 1000)%1000
		);
		printf("  DIFF to prev  %lld.%.03ld.%03ld",
			(long long)stats.diff.tv_sec,
			stats.diff.tv_nsec / 1000000,
			(stats.diff.tv_nsec / 1000)%1000
		);
		printf("  MEAN  %lld.%.03ld.%03ld",
			(long long)stats.mean.tv_sec,
			stats.mean.tv_nsec / 1000000,
			(stats.mean.tv_nsec / 1000)%1000
		);
		printf("  MAX   %lld.%.03ld.%03ld",
			(long long)stats.max.tv_sec,
			stats.max.tv_nsec / 1000000,
			(stats.max.tv_nsec / 1000)%1000
		);
		printf("\n");
#endif
		char str[1024];

		memset(str, 0, sizeof(str));
		snprintf(str, sizeof(str), "SEQ: %-d; TS(r): %lld.%.06ld; TS(r): %lld.%.06ld; DIFF: %lld.%.06ld; MEAN: %lld.%.06ld; MAX: %lld.%.06ld;\n",
				tp->seq,
				(long long)ts.tv_sec,
				(ts.tv_nsec / 1000),

				(long long)tp->ts.tv_sec,
				(tp->ts.tv_nsec / 1000),

				(long long)stats.diff.tv_sec,
				(stats.diff.tv_nsec / 1000),

				(long long)stats.mean.tv_sec,
				(stats.mean.tv_nsec / 1000),

				(long long)stats.max.tv_sec,
				(stats.max.tv_nsec / 1000)
		);

		printf("%s", str);


		nanosleep_until(&sleep_ts, o_interval_us * 1000);
	}

	return NULL;
}


#if 0
int run_thread(void)
{
	struct sched_param param;
	pthread_attr_t attr;
	pthread_t thread;
	int ret;

	/* Lock memory */
	if(mlockall(MCL_CURRENT|MCL_FUTURE) == -1) {
		printf("mlockall failed: %m\n");
		exit(-2);
	}

	/* Initialize pthread attributes (default values) */
	ret = pthread_attr_init(&attr);
	if (ret) {
		printf("init pthread attributes failed\n");
		goto out;
	}

	/* Set a specific stack size  */
	ret = pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN);
	if (ret) {
		printf("pthread setstacksize failed\n");
		goto out;
	}

	/* Set scheduler policy and priority of pthread */
	ret = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
	if (ret) {
		printf("pthread setschedpolicy failed\n");
		goto out;
	}
	param.sched_priority = 80;
	ret = pthread_attr_setschedparam(&attr, &param);
	if (ret) {
		printf("pthread setschedparam failed\n");
		goto out;
	}
	/* Use scheduling parameters of attr */
	ret = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	if (ret) {
		printf("pthread setinheritsched failed\n");
		goto out;
	}

	/* Create a pthread with specified attributes */
	ret = pthread_create(&thread, &attr, thread_func, NULL);
	if (ret) {
		printf("create pthread failed\n");
		goto out;
	}

	/* Join the thread and wait until it is done */
	ret = pthread_join(thread, NULL);
	if (ret)
		printf("join pthread failed: %m\n");
out:
	return ret;
}
#endif


int main(int argc, char **argv)
{
	int rv = 0;

	eth_t *eth;
	struct ifreq ifopts;
	size_t idx = 0;

	struct timespec ts;

	parse_command_line_options(&argc, argv);

	if (argc < 2) {
		usage();
		return -1;
	}

	if (o_packet_size == -1) {
		o_packet_size = 64;
	} else if (o_packet_size < 64) {
		printf("not supported packet size\n");
		return -1;
	}

    /* start configuration control task */
	start_config_control();

	eth = eth_open(argv[1]);
	if (eth == NULL) {
		perror("eth_open");
		return -1;
	}

//	configure_tx_timestamp(eth->fd, argv[1]);

	config_thread();
	memset(tp, 0, sizeof(struct ether_testpacket));

	/* determine own ethernet address */
	{
		memset(&ifopts, 0, sizeof(struct ifreq));
		strncpy(ifopts.ifr_name, argv[1], sizeof(ifopts.ifr_name));
		if (ioctl(eth->fd, SIOCGIFHWADDR, &ifopts) < 0) {
			perror("ioctl");
			return -1;
		}
	}


	/* destination MAC */
	if (ether_aton_r(o_destination_mac, (struct ether_addr*)&tp->hdr.ether_dhost) == NULL) {
		return -1;
	}

	/* source MAC */
	memcpy(tp->hdr.ether_shost, &ifopts.ifr_hwaddr.sa_data, ETH_ALEN);

	/* ethertype */
	tp->hdr.ether_type = 0x0808;



	if (o_interval_us) {

		if (o_run_timer) {
			//run_by_timer();

		} else if (o_run_thread) {
			//run_thread();

		} else {
			struct timespec sleep_ts;
			struct stats stats;

			memset(&stats, 0, sizeof(struct stats));

			clock_gettime(CLOCK_MONOTONIC, &sleep_ts);

			/* wait for millisecond == 0 */
			busy_poll();

			while (1) {

				if (o_pause_loop) {
					sleep(1);
					continue;
				}

				/* sync to desired millisecond start */
				wait_for_next_timeslice(o_interval_us / 1000);

				clock_gettime(CLOCK_REALTIME, &ts);
				calc_stats(&ts, &stats, o_interval_us);

				char str[1024];

				/* update new timestamp in packet */
				memcpy(&tp->ts, &ts, sizeof(struct timespec));

				memset(str, 0, sizeof(str));
				snprintf(str, sizeof(str), "SEQ: %-d; TS(l): %lld.%.06ld; TS(tx): %lld.%.06ld; DIFF: %lld.%.06ld; MEAN: %lld.%.06ld; MAX: %lld.%.06ld;\n",
						tp->seq,
						(long long)ts.tv_sec,
						(ts.tv_nsec / 1000),

						0ULL,
						0UL,

						(long long)stats.diff.tv_sec,
						(stats.diff.tv_nsec / 1000),

						(long long)stats.mean.tv_sec,
						(stats.mean.tv_nsec / 1000),

						(long long)stats.max.tv_sec,
						(stats.max.tv_nsec / 1000)
				);

				tp->interval_us = o_interval_us;

				eth_send(eth, buf, o_packet_size);

				if (o_verbose) {
					printf("%s", str);
				}

//				(void)__poll;
				//__poll(eth->fd);

//				while (!get_tx_timestamp(eth->fd)) {}


				/* sync to beginning of millisecond */
//				nanosleep_until(&sleep_ts, o_interval_us * 1000);
				// increase sequence number
				tp->seq++;
			}
		}

	} else {
		clock_gettime(CLOCK_REALTIME, &ts);

		printf("%lld.%.3ld.%3ld.%3ld\n",
			(long long)ts.tv_sec,
			ts.tv_nsec / 1000000,
			(ts.tv_nsec / 1000)%1000,
			ts.tv_nsec %1000
		);
		memcpy(buf+idx, &ts, sizeof(struct timespec));
		idx += sizeof(struct timespec);

		eth_send(eth, buf, o_packet_size);
	}

	return rv;
}
