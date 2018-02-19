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

#include <errno.h>
#include <inttypes.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <netinet/ether.h>
#include <netinet/in.h>
#include <netpacket/packet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>


#include <glib.h>
#include <glib/gprintf.h>

#include "config_control.h"
#include "data.h"
#include "stats.h"
#include "timer.h"


static gchar *help_description = NULL;
static gint o_verbose = 0;
static gint o_version = 0;
static gchar *o_destination_mac = "FF:FF:FF:FF:FF:FF";
static gint o_sched_prio = -1;
static int o_queue_prio = -1;
static gint o_memlock = 1;
static gint o_config_control_port = 0;

/*
    TODO:
    the configuration can be set by conifg thread .. use threadsafe access
    https://developer.gnome.org/glib/2.54/glib-Atomic-Operations.html
*/
gint o_interval_ms = 0;
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
            &o_interval_ms, "Interval in milli seconds", NULL },
    { "prio",        'p', 0, G_OPTION_ARG_INT,
            &o_sched_prio, "Set scheduler priority", NULL },
    { "queue-prio",  'Q', 0, G_OPTION_ARG_INT,
            &o_queue_prio, "Set skb priority", NULL },
    { "memlock",     'm', 0, G_OPTION_ARG_INT,
            &o_memlock, "Configure memlock (default is 1)", NULL },
    { "padding",     'P', 0, G_OPTION_ARG_INT,
            &o_packet_size, "Set the packet size", NULL },
    { "verbose",     'v', 0, G_OPTION_ARG_NONE,
            &o_verbose, "Be verbose", NULL },
    { "version",     'V', 0, G_OPTION_ARG_NONE,
            &o_version, "Show version inforamtion and exit", NULL },
    { NULL, 0, 0, 0, NULL, NULL, NULL }
};

gint parse_command_line_options(gint *argc, char **argv)
{
    GError *error = NULL;
    GOptionContext *context;

    context = g_option_context_new("DEVICE - transmit timestamped test packets");

    g_option_context_add_main_entries(context, entries, NULL);
    g_option_context_set_description(context,
        "This tool sends ethernet test packets.\n"
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
        if (mlockall(MCL_CURRENT|MCL_FUTURE) == -1) {
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

void busy_poll(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    while (((ts.tv_nsec / 1000) % 1000) != 0) {
        clock_gettime(CLOCK_REALTIME, &ts);
    }

}

void print_packet_info(struct timespec *ts, struct stats *stats)
{
    char str[1024];

    memset(str, 0, sizeof(str));
    snprintf(str, sizeof(str),
            "SEQ: %-d; TS(l): %lld.%.06ld; TS(tx): %lld.%.06ld; "
            "DIFF: %lld.%.06ld; MEAN: %lld.%.06ld; MAX: %lld.%.06ld;\n",
            tp->seq,
            (long long)ts->tv_sec,
            (ts->tv_nsec / 1000),
            0ULL,
            0UL,
            (long long)stats->diff.tv_sec,
            (stats->diff.tv_nsec / 1000),
            (long long)stats->mean.tv_sec,
            (stats->mean.tv_nsec / 1000),
            (long long)stats->max.tv_sec,
            (stats->max.tv_nsec / 1000)
    );

    printf("%s", str);
}

int main(int argc, char **argv)
{
    int rv = 0;

    eth_t *eth;
    struct ifreq ifopts;

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
    if (o_config_control_port) {
        start_config_control();
    }

    eth = eth_open(argv[1]);
    if (eth == NULL) {
        perror("eth_open");
        return -1;
    }

    /* Set skb priority */
    if (o_queue_prio > 0) {
        setsockopt(eth->fd, SOL_SOCKET, SO_PRIORITY, &o_queue_prio, sizeof(o_queue_prio));
    }


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
    if (ether_aton_r(o_destination_mac,
            (struct ether_addr*)&tp->hdr.ether_dhost) == NULL) {
        printf("ether_aton_r: failed\n");
        return -1;
    }

    /* source MAC */
    memcpy(tp->hdr.ether_shost, &ifopts.ifr_hwaddr.sa_data, ETH_ALEN);

    /* ethertype */
    tp->hdr.ether_type = TEST_PACKET_ETHER_TYPE;

    if (o_interval_ms) {
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
            wait_for_next_timeslice(o_interval_ms, &tp->ts_desired);

            clock_gettime(CLOCK_REALTIME, &ts);
            calc_stats(&ts, &stats, o_interval_ms * 1000);

            /* update new timestamp in packet */
            memcpy(&tp->ts, &ts, sizeof(struct timespec));

            tp->interval_us = o_interval_ms * 1000;
            tp->packet_size = o_packet_size;

            eth_send(eth, buf, o_packet_size);

            if (o_verbose) {
                print_packet_info(&ts, &stats);
            }

            tp->seq++;
        }

    } else {
        clock_gettime(CLOCK_REALTIME, &ts);
        memcpy(&tp->ts, &ts, sizeof(struct timespec));

        eth_send(eth, buf, o_packet_size);

        printf("%lld.%.3ld.%3ld.%3ld\n",
            (long long)ts.tv_sec,
            ts.tv_nsec / 1000000,
            (ts.tv_nsec / 1000)%1000,
            ts.tv_nsec %1000
        );
    }

    return rv;
}
