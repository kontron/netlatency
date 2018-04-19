/*
 * Copyright (c) 2018, Kontron Europe GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#define GNU_SOURCE
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <netinet/ether.h>
#include <netinet/in.h>
#include <netpacket/packet.h>
#include <linux/net_tstamp.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <linux/version.h>

#include <glib.h>
#include <glib/gprintf.h>

#include <jansson.h>

#include "data.h"
#include "timer.h"

#ifndef VERSION
#define VERSION "dev"
#endif

static gchar *help_description = NULL;
static gchar *o_destination_mac = "FF:FF:FF:FF:FF:FF";
static gint o_count = 0;
static gint o_cpu_number = -1;
static gint o_interval_ms = 1000;
static gint o_interval_offset_usec = 0;
static gint o_padding = -1;
static gint o_sched_prio = 99;
static gint o_stream_id = 0;
static gint o_verbose = 0;
static gint o_version = 0;
static gint o_small_pkt_mode = 0;
static int o_queue_prio = -1;

static gint do_shutdown = 0;

static char tp_buf[1518];
struct ether_testpacket *tp = (void*)tp_buf;

int eth_open(const char *device)
{
    int fd;
    struct sockaddr_ll sll;
    struct ifreq ifr;

    if ((fd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) < 0) {
        close(fd);
        return -1;
    }

    strncpy(ifr.ifr_name, device, sizeof(ifr.ifr_name));

    /* terminate string with 0 */
    ifr.ifr_name[sizeof(ifr.ifr_name)-1] = 0;

    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        close(fd);
        return -1;
    }

    sll.sll_family = AF_PACKET;
    sll.sll_ifindex = ifr.ifr_ifindex;

    bind(fd, (struct sockaddr *) &sll, sizeof(sll));

    return fd;
}

void usage(void)
{
    g_printf("%s", help_description);
}

static GOptionEntry entries[] = {
    { "destination", 'd', 0, G_OPTION_ARG_STRING,
            &o_destination_mac,
            "Destination MAC address", "MAC"},
    { "interval",    'i', 0, G_OPTION_ARG_INT,
            &o_interval_ms,
            "Interval in milli seconds (default is 1000)", "INTERVAL" },
    { "stream-id",   'I', 0, G_OPTION_ARG_INT,
            &o_stream_id,
            "Set stream id (default is 0)", "ID" },
    { "count",       'c', 0, G_OPTION_ARG_INT,
            &o_count,
            "Transmit packet count", "COUNT" },
    { "cpu",         'C', 0, G_OPTION_ARG_INT,
            &o_cpu_number,
            "Run timer thread on specified CPU(s) (default is all)", "CPU" },
    { "padding",     'P', 0, G_OPTION_ARG_INT,
            &o_padding,
            "Pad packet to given size", "SIZE" },
    { "prio",        'p', 0, G_OPTION_ARG_INT,
            &o_sched_prio,
            "Set scheduler priority (default is 99)", "PRIO" },
    { "offset",      'O', 0, G_OPTION_ARG_INT,
            &o_interval_offset_usec,
            "Set timer interval offset value in usec", "OFFSET" },
    { "queue-prio",  'Q', 0, G_OPTION_ARG_INT,
            &o_queue_prio,
            "Set skb priority", "PRIO" },
    { "small-pkt-mode", 'S', 0, G_OPTION_ARG_NONE,
            &o_small_pkt_mode,
            "Send small packets (<64 bytes), only include important timestamps", NULL },
    { "verbose",     'v', 0, G_OPTION_ARG_NONE,
            &o_verbose,
            "Be verbose", NULL },
    { "version",     'V', 0, G_OPTION_ARG_NONE,
            &o_version,
            "Show version information and exit", NULL },
    { NULL, 0, 0, 0, NULL, NULL, NULL }
};

gint parse_command_line_options(gint *argc, char **argv)
{
    GError *error = NULL;
    GOptionContext *context;

    context = g_option_context_new(
                    "interface");
    g_option_context_set_summary(context,
            "Generate test packets that contains additional information for\n"
            "latency measurements.");

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

static int latency_target_fd = -1;
static gint32 latency_target_value = 0;

/* Latency trick
 * if the file /dev/cpu_dma_latency exists,
 * open it and write a zero into it. This will tell
 * the power management system not to transition to
 * a high cstate (in fact, the system acts like idle=poll)
 * When the fd to /dev/cpu_dma_latency is closed, the behavior
 * goes back to the system default.
 *
 * Documentation/power/pm_qos_interface.txt
 */
static void set_latency_target(gint32 latency_value)
{
    struct stat s;
    int err;

    errno = 0;
    err = stat("/dev/cpu_dma_latency", &s);
    if (err == -1) {
        perror("stat /dev/cpu_dma_latency failed");
        return;
    }

    errno = 0;
    latency_target_fd = open("/dev/cpu_dma_latency", O_RDWR);
    if (latency_target_fd == -1) {
        perror("open /dev/cpu_dma_latency");
        return;
    }

    errno = 0;
    err = write(latency_target_fd, &latency_value, 4);
    if (err < 1) {
        perror("error setting cpu_dma_latency");
        close(latency_target_fd);
        return;
    }
}

void signal_handler(int signal)
{
    switch (signal) {
    case SIGINT:
    case SIGTERM:
        munlockall();
        do_shutdown++;
    break;
    case SIGUSR1:
    break;
    default:
    break;
    }

}

struct thread_param {
    int fd;
};

struct thread_param thread_param;

static void tp_set_timestamp(struct ether_testpacket *tp, int tsnum, struct
        timespec *ts)
{
    if (ts == NULL) {
        clock_gettime(CLOCK_REALTIME, &tp->timestamps[tsnum]);
    } else {
        memcpy(&tp->timestamps[tsnum], ts, sizeof(struct timespec));
    }
}

static void get_tx_timestamps(int fd, struct timespec *ts1,
        struct timespec *ts2)
{
    struct msghdr msg;
    char control[256];
    char buf[512];
    struct iovec iov = { buf, sizeof(buf) };
    struct cmsghdr *cm;
    struct timespec *_ts;

    memset(ts1, 0, sizeof(*ts1));
    memset(ts2, 0, sizeof(*ts2));

    memset(control, 0, sizeof(control));
    memset(&msg, 0, sizeof(msg));

    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    /*
     * Unfortunately the kernel doesn't tell us the type of the timestamp.
     * There is no sock_extended_err CMSG for a AF_PACKET socket. Might be a
     * bug in the kernel. Therefore, we assume that the first timestamp we
     * receive is the one of the network scheduler and the second one the one
     * of the driver.
     */
    while (recvmsg(fd, &msg, MSG_DONTWAIT | MSG_ERRQUEUE) > 0) {
        for (cm = CMSG_FIRSTHDR(&msg); cm != NULL; cm = CMSG_NXTHDR(&msg, cm)) {
            if (cm->cmsg_level == SOL_SOCKET
                    && cm->cmsg_type == SO_TIMESTAMPING
                    && cm->cmsg_len >= sizeof(struct timespec) * 3) {
                _ts = (struct timespec *)CMSG_DATA(cm);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
                *ts1 = *ts2;
#endif
                *ts2 = *_ts;
            }
        }
    }
}

static void *timer_thread(void *params)
{
    struct thread_param *parm = params;
    struct sched_param schedp;
    struct timespec next;
    struct timespec interval_start;
    struct timespec interval;
    struct timespec last_sched_tx_ts;
    struct timespec last_sw_tx_ts;
    gint64 count = 0;
    int size;

    pthread_setname_np(pthread_self(), "TX RT thread");

    if (o_cpu_number != -1) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(o_cpu_number, &cpuset);
        if (pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset)) {
            perror("error setting cpu affinity");
        }

    }

#if 0
    {
        cpu_set_t cpuset;
        pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        int i;
        for (i=0; i < CPU_SETSIZE; i++) {
            if (CPU_ISSET(i, &cpuset)) {
                printf("cpu mask: %x\n", i);
            }
        }
    }
#endif

    memset(&schedp, 0, sizeof(schedp));
    schedp.sched_priority = o_sched_prio;
    if (sched_setscheduler(0, SCHED_FIFO, &schedp)) {
        perror("failed to set scheduler policy");
    }

    interval.tv_sec = 0;
    interval.tv_nsec = o_interval_ms * 1000000;

    tp->interval_usec = o_interval_ms * 1000;
    tp->offset_usec = o_interval_offset_usec;
    tp->stream_id = o_stream_id;
    tp->version = 1;

    while (!do_shutdown) {
        /* get timestamp of last transmitted packet */
        get_tx_timestamps(parm->fd, &last_sched_tx_ts, &last_sw_tx_ts);
        size = sizeof(struct ether_testpacket);

        /* if interval is 0 send as fast as possible */
        if (o_interval_ms != 0) {
            wait_for_next_timeslice(&interval, o_interval_offset_usec,
                    &next, &interval_start);
        }

        /* update timestamps in packet */
        tp_set_timestamp(tp, TS_WAKEUP, NULL);

        tp_set_timestamp(tp, TS_T0, &interval_start);
        if (!o_small_pkt_mode) {
            tp_set_timestamp(tp, TS_LAST_KERNEL_SCHED, &last_sched_tx_ts);
            tp_set_timestamp(tp, TS_LAST_KERNEL_SW_TX, &last_sw_tx_ts);
            tp_set_timestamp(tp, TS_PROG_SEND, NULL);
            size += sizeof(struct timespec) * 5;
        } else {
            size += sizeof(struct timespec);
        }

        send(parm->fd, (char*)tp, MAX(size, o_padding), 0);

        tp->seq++;
        count++;

        if (o_count && count >= o_count) {
            do_shutdown++;
            break;
        }
    }

    return NULL;
}

static void show_version(void)
{
    g_printf("%s\n", VERSION);
}

int main(int argc, char **argv)
{
    int rv = 0;
    int fd;
    struct ifreq ifopts;
    sigset_t sigset;
    pthread_t thread;
    pthread_attr_t attr;
    int opt;
    int rc;

    parse_command_line_options(&argc, argv);

    if (o_version) {
        show_version();
        return 0;
    }

    if (argc < 2) {
        usage();
        return -1;
    }

    fd = eth_open(argv[1]);
    if (fd < 0) {
        perror("eth_open");
        return -1;
    }

    /* Set skb priority */
    if (o_queue_prio > 0) {
        setsockopt(fd, SOL_SOCKET, SO_PRIORITY, &o_queue_prio,
                   sizeof(o_queue_prio));
    }

    /* enable transmit timestamping */
    opt = SOF_TIMESTAMPING_TX_SOFTWARE
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
          | SOF_TIMESTAMPING_TX_SCHED
#endif
          | SOF_TIMESTAMPING_SOFTWARE;
    rc = setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING, &opt, sizeof(opt));
    if (rc == -1) {
        perror("setsockopt() ... enable timestamp");
        return -1;
    }

    /* use the /dev/cpu_dma_latency trick if it's there */
    set_latency_target(latency_target_value);


    if (mlockall(MCL_CURRENT|MCL_FUTURE) == -1) {
        perror("mlockal");
        exit(-2);
    }

    memset(tp, 0, sizeof(struct ether_testpacket));

    /* determine own ethernet address */
    {
        memset(&ifopts, 0, sizeof(struct ifreq));
        strncpy(ifopts.ifr_name, argv[1], sizeof(ifopts.ifr_name));
        if (ioctl(fd, SIOCGIFHWADDR, &ifopts) < 0) {
            perror("ioctl");
            return -1;
        }
    }


    /* destination MAC */
    if (ether_aton_r(o_destination_mac,
            (struct ether_addr*)&tp->hdr.ether_dhost) == NULL) {
        perror("ether_aton_r: failed");
        return -1;
    }

    /* source MAC */
    memcpy(tp->hdr.ether_shost, &ifopts.ifr_hwaddr.sa_data, ETH_ALEN);

    /* ethertype */
    tp->hdr.ether_type = ntohs(TEST_PACKET_ETHER_TYPE);

    sigemptyset(&sigset);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGUSR1, signal_handler);

    rv = pthread_attr_init(&attr);
    if (rv) {
        perror("pthread_attr_init");
        return -1;
    }
    thread_param.fd = fd;

    rv = pthread_create(&thread, &attr, timer_thread, &thread_param);

    while (!do_shutdown) {
        usleep(10000);

        if (do_shutdown) {
            break;
        }
    }

    pthread_join(thread, NULL);

    return rv;
}
