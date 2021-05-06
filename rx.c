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

#include <assert.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/filter.h>
#include <linux/if_packet.h>
#include <linux/net_tstamp.h>
#include <linux/sockios.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gprintf.h>

#include <jansson.h>

#include "data.h"
#include "timer.h"

#ifndef VERSION
#define VERSION "dev"
#endif

#define MAX_STREAM_ID 16

enum {
    TS_KERNEL_HW_RX,
    TS_KERNEL_SW_RX,
    TS_PROG_RECV,

    MAX_TS_RX
};
struct result {
    struct ether_testpacket *tp;
    struct ether_testpacket *last_tp;

    struct timespec *rx_tss;
    struct timespec *last_rx_tss;

    gint dropped;
    gboolean seq_error;
};

static struct result results[MAX_STREAM_ID];

static gchar *help_description = NULL;
static gint o_capture_ethertype = TP_ETHER_TYPE;
static gint o_count = 0;
static gint o_ptp_mode = FALSE;
static gint o_no_hw_ts = FALSE;
static gint o_rx_filter = HWTSTAMP_FILTER_ALL;
static gint o_verbose = 0;
static gint o_version = 0;
static gint count = 0;

static gboolean do_shutdown = FALSE;

static void get_hw_timestamps(struct msghdr *msg, struct timespec *ts1, struct timespec *ts2)
{
    struct cmsghdr *cmsg;

    struct scm_timestamping {
        struct timespec ts[3];
    };

    memset(ts1, 0, sizeof(struct timespec));
    memset(ts2, 0, sizeof(struct timespec));

    for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
        struct scm_timestamping* scm_ts = NULL;

        if (cmsg->cmsg_level != SOL_SOCKET) {
            continue;
        }

        switch (cmsg->cmsg_type) {
        case SO_TIMESTAMP:
            break;
        case SO_TIMESTAMPNS:
            break;
        case SO_TIMESTAMPING:
            scm_ts = (struct scm_timestamping*) CMSG_DATA(cmsg);
            memcpy(ts1, &scm_ts->ts[0], sizeof(struct timespec));
            memcpy(ts2, &scm_ts->ts[2], sizeof(struct timespec));
            break;
        default:
            printf("cmsg_type=%d", cmsg->cmsg_type);
            /* Ignore other cmsg options */
            break;
        }
    }
}

static gboolean is_broadcast_addr(guint8 *addr)
{
    return !memcmp(addr, "\xff\xff\xff\xff\xff\xff", ETH_ALEN);
}

static struct msghdr *receive_msg(int fd, struct ether_addr *myaddr)
{
    static struct msghdr msg;
    static struct iovec iov;
    static unsigned char buf[2048];
    static char cbuf[1024];
    struct sockaddr_in host_address;
    struct ether_testpacket *tp = (void*)buf;
    int n;

    /* recvmsg header structure */
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_name = &host_address;
    msg.msg_namelen = sizeof(struct sockaddr_in);
    msg.msg_control = cbuf;
    msg.msg_controllen = sizeof(cbuf);

    /* block for message */
    n = recvmsg(fd, &msg, 0);
    if ( n == -1 && errno == EAGAIN ) {
        return 0;
    }

    if (myaddr != NULL) {
        /* filter for own ether packets */
        if (is_broadcast_addr(tp->hdr.ether_dhost)) {
            return &msg;
        }
        if (memcmp(myaddr->ether_addr_octet, tp->hdr.ether_dhost, ETH_ALEN)) {
            return NULL;
        }
    }

    return &msg;
}

static int check_sequence_num(struct result *result)
{
    if (!result->last_tp) {
        result->dropped = 0;
        result->seq_error = 0;
    } else {
        result->dropped = MAX((gint)result->tp->seq - (gint)result->last_tp->seq - 1, 0);
        result->seq_error = result->tp->seq <= result->last_tp->seq;
    }

    return result->dropped || result->seq_error;
}

static int handle_test_packet(struct msghdr *msg,
        struct result *result)
{
    struct ether_testpacket *tp = (void*)msg->msg_iov->iov_base;
    int rc;

    /* ignore future packet versions */
    if (tp->version != 1) {
        return 0;
    }

    /* remember test packet */
    g_free(result->last_tp);
    g_free(result->last_rx_tss);
    result->last_tp = result->tp;
    result->last_rx_tss = result->rx_tss;
    result->tp = g_memdup(tp, sizeof(*tp));
    result->rx_tss = g_new0(struct timespec, MAX_TS_RX);

    /* get rx timestamp */
    clock_gettime(CLOCK_REALTIME, &result->rx_tss[TS_PROG_RECV]);

    get_hw_timestamps(msg,
            &result->rx_tss[TS_KERNEL_SW_RX],
            &result->rx_tss[TS_KERNEL_HW_RX]);

    /* calc dropped count and sequence error */
    rc = check_sequence_num(result);

    /* if there was an error discard last_tp */
    if (rc) {
        g_free(result->last_tp);
        g_free(result->last_rx_tss);
        result->last_tp = NULL;
        result->last_rx_tss = NULL;
    }

    return 0;
}

static int add_json_timestamp(json_t *object, char *name, struct timespec ts)
{
    char *s;
    json_t *n;
    json_t *v;

    /* Add to names array */
    n = json_object_get(object, "names");
    if (n == NULL) {
        assert(0);
    }
    json_array_append_new(n, json_string(name));

    /* Add to values array */
    v = json_object_get(object, "values");
    if (v == NULL) {
        assert(0);
    }

    s = timespec_to_iso_string(&ts);
    json_array_append_new(v, json_string(s));
    g_free(s);

    return 0;
}

static json_t *json_test_packet(struct ether_testpacket *tp1,
        struct ether_testpacket *tp2, struct timespec *tss)
{
    g_assert(tp1);
    g_assert(tp2);
    g_assert(tss);

    json_t *root = json_object();
    json_t *object = json_object();
    json_t *timestamps = json_object();

    json_object_set_new(root, "type", json_string("rx-packet"));
    json_object_set_new(root, "object", object);

    json_object_set_new(object, "stream-id", json_integer(tp1->stream_id));
    json_object_set_new(object, "sequence-number", json_integer(tp1->seq));
    json_object_set_new(object, "interval-usec", json_integer(tp1->interval_usec));
    json_object_set_new(object, "offset-usec", json_integer(tp1->offset_usec));

    json_object_set_new(object, "timestamps", timestamps);
    json_object_set_new(timestamps, "names", json_array());
    json_object_set_new(timestamps, "values", json_array());

    /* copy the timestamps to ts to avoid unaligned pointer compiler errors */
    add_json_timestamp(timestamps, "interval-start", tp1->timestamps[TS_T0]);
    add_json_timestamp(timestamps, "tx-wakeup", tp1->timestamps[TS_WAKEUP]);
    add_json_timestamp(timestamps, "tx-program", tp1->timestamps[TS_PROG_SEND]);
    add_json_timestamp(timestamps, "tx-kernel-netsched", tp2->timestamps[TS_LAST_KERNEL_SCHED]);
    add_json_timestamp(timestamps, "tx-kernel-hardware", tp2->timestamps[TS_LAST_KERNEL_SW_TX]);

    add_json_timestamp(timestamps, "rx-hardware", tss[TS_KERNEL_HW_RX]);
    //add_json_timestamp(timestamps, "rx-kernel-driver", &tss[TS_KERNEL_SW_RX]);
    add_json_timestamp(timestamps, "rx-program", tss[TS_PROG_RECV]);

    return root;
}

static json_t *json_error(struct result *result)
{
    json_t *j;

    j = json_pack("{sss{sisb}}",
                  "type", "rx-error",
                  "object",
                  "dropped-packets", result->dropped,
                  "sequence-error", result->seq_error
    );

    return j;
}

static void dump_json_stdout(struct json_t *j)
{
    char *s = json_dumps(j, JSON_COMPACT);
    if (s) {
        printf("%s\n", s);
        fflush(stdout);
        free(s);
    }
}

static int handle_msg(struct msghdr *msg)
{
    json_t *j;

    struct ether_header *hdr = msg->msg_iov->iov_base;
    guint16 ethertype = ntohs(hdr->ether_type);
    struct result *result;

    /* build result message string */
    switch (ethertype) {
    case TP_ETHER_TYPE: {
        struct ether_testpacket *tp = (void*)hdr;
        int stream_id = tp->stream_id;
        result = &results[stream_id];

        /* ignore packets with large stream ids */
        if (stream_id > MAX_STREAM_ID) {
            return 0;
        }

        handle_test_packet(msg, result);

        if (result->dropped || result->seq_error) {
            j = json_error(result);
            dump_json_stdout(j);
            json_decref(j);
        }

        /* we have to wait for at least two packets */
        if (result->last_tp) {
            j = json_test_packet(result->last_tp, result->tp, result->last_rx_tss);
            dump_json_stdout(j);
            json_decref(j);

            if (o_count && ++count >= o_count) {
                do_shutdown = TRUE;
                return 0;
            }
        }

        /* or we've received the last packet */
        if (result->tp->flags & TP_FLAG_END_OF_STREAM) {
            struct ether_testpacket *tp_dummy = g_new0(struct ether_testpacket, 1);
            j = json_test_packet(result->tp, tp_dummy, result->rx_tss);
            g_free(tp_dummy);
            dump_json_stdout(j);
            json_decref(j);

            do_shutdown = TRUE;
            return 0;
        }

        break;
    }
    default:
        printf("tbd ... other packet\n");
        break;
    }

    return 0;
}

static int get_own_eth_address(int fd, gchar *ifname, struct ether_addr *src_eth_addr)
{
    struct ifreq ifopts;

    /* determine own ethernet address */
    memset(&ifopts, 0, sizeof(struct ifreq));
    if (strlen(ifname) < sizeof(ifopts.ifr_name)) {
        memcpy(ifopts.ifr_name, ifname, strlen(ifname));
        if (ioctl(fd, SIOCGIFHWADDR, &ifopts) < 0) {
            perror("ioctl");
            return -1;
        }
    } else {
        perror("ifname too long");
        return -1;
    }

    memcpy(src_eth_addr, &ifopts.ifr_hwaddr.sa_data, ETH_ALEN);

    return 0;
}

static int setsockopt_reuseaddr(int fd)
{
    int rc;
    int opt = 0;

    rc = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
    if (rc == -1) {
        perror("setsockopt(SO_REUSEADDR)");
        close(fd);
        return -1;
    }

    return rc;
}

static int setsockopt_rx_timestamping(int fd)
{
    int rc;
    int opt = 0;

    opt = SOF_TIMESTAMPING_RX_HARDWARE
          | SOF_TIMESTAMPING_RAW_HARDWARE
          | SOF_TIMESTAMPING_SYS_HARDWARE
          | SOF_TIMESTAMPING_SOFTWARE;
    rc = setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING, &opt, sizeof(opt));
    if (rc == -1) {
        perror("setsockopt(SO_TIMESTAMPING)");
        return -1;
    }

    return rc;
}

static int set_hwtimestamping(int fd, gchar *ifname)
{
    struct ifreq ifr;
    struct hwtstamp_config config;

    config.flags = 0;
    config.tx_type = HWTSTAMP_TX_ON;
    config.rx_filter = o_rx_filter;
    if (config.tx_type < 0 || config.rx_filter < 0) {
        return -1;
    }

    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifname);
    ifr.ifr_data = (caddr_t)&config;
    if (ioctl(fd, SIOCSHWTSTAMP, &ifr)) {
        perror("ioctl() ... configure timestamping\n");
        return -1;
    }

    return 0;
}

static int open_capture_interface(gchar *ifname)
{
    int fd;
    struct sockaddr_ll sock_address;

    fd = socket(PF_PACKET, SOCK_RAW, htons(o_capture_ethertype));
    if (fd < 0) {
        perror("socket()");
        return -1;
    }

    setsockopt_reuseaddr(fd);

    if (!o_no_hw_ts) {
		set_hwtimestamping(fd, ifname);
        setsockopt_rx_timestamping(fd);
    }

    /* Bind to device */
    memset(&sock_address, 0, sizeof(sock_address));
    sock_address.sll_family = PF_PACKET;
    sock_address.sll_protocol = htons(o_capture_ethertype);
    sock_address.sll_ifindex = if_nametoindex(ifname);

    if (bind(fd, (struct sockaddr*) &sock_address, sizeof(sock_address)) < 0) {
        perror("bind failed\n");
        close(fd);
        return -4;
    }

    return fd;
}

static int flush_socket(int fd)
{
    int rc;
    int opt;

    /* filter instrucion to drop all packets */
    struct sock_filter insn = BPF_STMT(BPF_RET | BPF_K, 0);
    struct sock_fprog fcode = { 1, &insn };

    /* filter out all incoming packets */
    rc = setsockopt(fd, SOL_SOCKET, SO_ATTACH_FILTER, &fcode, sizeof(fcode));
    if (rc == -1) {
        perror("setsockopt(SO_ATTACH_FILTER)");
        return -1;
    }

    /* drain receive queue */
    while (recv(fd, NULL, 0, MSG_TRUNC | MSG_DONTWAIT) > 0);

    /* disable filter again */
    opt = 0;
    rc = setsockopt(fd, SOL_SOCKET, SO_DETACH_FILTER, &opt, sizeof(opt));
    if (rc == -1) {
        perror("setsockopt(SO_DETACH_FILTER)");
        return -1;
    }

    return rc;
}

void usage(void)
{
    g_printf("%s", help_description);
}


#define MACROSTR(k) { k, #k }
struct filter_map {
    int filter;
    char *name;
} filter_map[] = {
    MACROSTR(HWTSTAMP_FILTER_ALL),
    MACROSTR(HWTSTAMP_FILTER_SOME),
    MACROSTR(HWTSTAMP_FILTER_PTP_V1_L4_EVENT),
    MACROSTR(HWTSTAMP_FILTER_PTP_V1_L4_SYNC),
    MACROSTR(HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ),
    MACROSTR(HWTSTAMP_FILTER_PTP_V2_L4_EVENT),
    MACROSTR(HWTSTAMP_FILTER_PTP_V2_L4_SYNC),
    MACROSTR(HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ),
    MACROSTR(HWTSTAMP_FILTER_PTP_V2_L2_EVENT),
    MACROSTR(HWTSTAMP_FILTER_PTP_V2_L2_SYNC),
    MACROSTR(HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ),
    MACROSTR(HWTSTAMP_FILTER_PTP_V2_EVENT),
    MACROSTR(HWTSTAMP_FILTER_PTP_V2_SYNC),
    MACROSTR(HWTSTAMP_FILTER_PTP_V2_DELAY_REQ),
    {0, NULL}
};

static gboolean parse_rx_filter_cb(const gchar *key, const gchar *value,
    gpointer user_data, GError *error)
{
    int i;
    (void)key;
    (void)user_data;
    (void)error;

    for (i = 0; filter_map[i].name != NULL; i++) {
        if (!strncmp(filter_map[i].name, value, strlen(value))) {
            o_rx_filter = filter_map[i].filter;
            return TRUE;
        }
    }

    /* no valid found .. print valid options */
    printf("valid rx filters are:\n");
    for (i = 0; filter_map[i].name != NULL; i++) {
        printf("  %s\n", filter_map[i].name);
    }

    return FALSE;
}


static GOptionEntry entries[] = {
    { "verbose",   'v', 0, G_OPTION_ARG_NONE,
            &o_verbose, "Be verbose", NULL },
    { "count",    'c', 0, G_OPTION_ARG_INT,
            &o_count,
            "Receive packet count", "COUNT" },
    { "ethertype", 'e', 0, G_OPTION_ARG_INT,
            &o_capture_ethertype, "Set ethertype to filter"
            " (Default is 0x0808, ETH_P_ALL is 0x3)", "TYPE" },
    { "rxfilter", 'f', 0, G_OPTION_ARG_CALLBACK,
            parse_rx_filter_cb, "Set HW rx filter", "FILTER" },
    { "ptp", 'p', 0, G_OPTION_ARG_NONE,
            &o_ptp_mode, "Set HW rx filter to PTP packets", NULL },
    { "no-hw-ts", 'n', 0, G_OPTION_ARG_NONE,
            &o_no_hw_ts, "Do not read HW timestamps", NULL },
    { "version",   'V', 0, G_OPTION_ARG_NONE,
            &o_version, "Show version information and exit", NULL },
    { NULL, 0, 0, 0, NULL, NULL, NULL }
};

static gint parse_command_line_options(gint *argc, char **argv)
{
    GError *error = NULL;
    GOptionContext *context;

    context = g_option_context_new("DEVICE - receive ethernet test packets");

    g_option_context_add_main_entries(context, entries, NULL);
    g_option_context_set_description(context,
        "This tool receives and analyzes incoming ethernet test packets.\n"
    );

    if (!g_option_context_parse(context, argc, &argv, &error)) {
        g_print("option parsing failed: %s\n", error->message);
        exit(1);
    }

    help_description = g_option_context_get_help(context, 0, NULL);
    g_option_context_free(context);

    return 0;
}

static void signal_handler(int signal)
{
    switch (signal) {
    case SIGINT:
    case SIGTERM:
        exit(1);
    break;
    case SIGUSR1:
    break;
    default:
    break;
    }
}

static void show_version(void)
{
    g_printf("%s\n", VERSION);
}

int real_main(int argc, char **argv)
{
    int rc;
    int fd;
    struct ether_addr *src_eth_addr = NULL;
    char *ifname = NULL;
    sigset_t sigset;
    struct msghdr *msg;

    parse_command_line_options(&argc, argv);

    if (o_version) {
        show_version();
        return 0;
    }

    if (argc < 2) {
        usage();
        return -1;
    }

    ifname = argv[1];

    if (o_ptp_mode) {
        o_capture_ethertype = ETH_P_1588;
        o_rx_filter = HWTSTAMP_FILTER_PTP_V2_L4_EVENT;
    }

    fd = open_capture_interface(ifname);
    if (fd < 0) {
        perror("open_capture_interface()");
        return -1;
    }

    if (o_ptp_mode == FALSE) {
        struct ether_addr my_eth_addr;
        rc = get_own_eth_address(fd, ifname, &my_eth_addr);
        if (rc) {
            perror("get_own_eth_address() ... bind to device");
            close(fd);
            exit(EXIT_FAILURE);
        }
    }

    sigemptyset(&sigset);
//  sigaddset(&sigset, SIGALARM);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGUSR1, signal_handler);

    rc = flush_socket(fd);
    if (rc) {
        close(fd);
        return EXIT_FAILURE;
    }

    while (!do_shutdown) {
        msg = receive_msg(fd, src_eth_addr);
        if (msg) {
            handle_msg(msg);
        }
    }

    close(fd);

    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    return real_main(argc, argv);
}
