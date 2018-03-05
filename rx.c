/*
** Copyright 2017       Kontron Europe GmbH
** Copyright 2005-2016  Solarflare Communications Inc.
**                      7505 Irvine Center Drive, Irvine, CA 92618, USA
** Copyright 2002-2005  Level 5 Networks Inc.
**
** This program is free software; you can redistribute it and/or modify it
** under the terms of version 2 of the GNU General Public License as
** published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*/

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/if_packet.h>
#include <linux/net_tstamp.h>
#include <linux/sockios.h>
#include <net/ethernet.h>
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
#include "domain_socket.h"
#include "timer.h"


static gchar *help_description = NULL;
static gint o_verbose = 0;
static gint o_quiet = 0;
static gint o_version = 0;
static gint o_socket = 0;
static gint o_histogram = 0;
static gint o_capture_ethertype = TEST_PACKET_ETHER_TYPE;
static gint o_rx_filter = HWTSTAMP_FILTER_ALL;
static gint o_ptp_mode = FALSE;

#define HISTOGRAM_VALUES_MAX 1000
struct histogram {
    gint32 array[HISTOGRAM_VALUES_MAX];
    gint32 outliers;
    gint32 min;
    gint32 max;
    gint32 count;
};

struct histogram histogram = {
    .array = {0},
    .outliers = 0,
    .min = 0,
    .max = 0,
    .count = 0,
};

static void get_hw_timestamp(struct msghdr *msg, struct timespec *ts)
{
    struct cmsghdr *cmsg;

    struct scm_timestamping {
        struct timespec ts[3];
    };

    memset(ts, 0, sizeof(struct timespec));

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
            memcpy(ts, &scm_ts->ts[2], sizeof(struct timespec));
            break;
        default:
            printf("cmsg_type=%d", cmsg->cmsg_type);
            /* Ignore other cmsg options */
            break;
        }
    }
}

#define BUF_SIZE 10*1024
#define BUF_CONTROL_SIZE 1024
static int receive_msg(int fd, struct ether_addr *myaddr, struct msghdr *msg)
{
    int n;

    /* block for message */
    n = recvmsg(fd, msg, 0);
    if ( n == 0 && errno == EAGAIN ) {
        return 0;
    }

    struct ether_testpacket *tp;
    tp = (struct ether_testpacket*)msg->msg_iov->iov_base;


    if (myaddr != NULL) {
        /* filter for own ether packets */
        if (memcmp(myaddr->ether_addr_octet, tp->hdr.ether_dhost, ETH_ALEN)) {
            return -1;
        }
    }

    return n;
}

#define MAX_CLIENTS 2
static int client_socket[MAX_CLIENTS];
static int max_fd;

static int check_sequence_num(unsigned long seq, long *dropped_packets,
        gboolean *sequence_error)
{
    static unsigned long last_seq = 0;

    if (last_seq == 0) {
        last_seq = seq;
        *dropped_packets = 0;
        *sequence_error = 0;
        return 0;
    }

    *dropped_packets = seq - last_seq - 1;
    *sequence_error = seq <= last_seq;

    if (*dropped_packets || *sequence_error) {
        printf("dropped=%ld error=%d\n", *dropped_packets, *sequence_error);
    }

    last_seq = seq;
    return 0;
}

struct test_packet_result {
    uint32_t seq;
    uint32_t interval_us;
    uint32_t packet_size;

    struct timespec diff_ts;
    uint32_t abs_ns;
    long dropped;
    gboolean seq_error;
};

static int handle_test_packet(struct msghdr *msg,
        struct test_packet_result *result)
{
    struct ether_testpacket *tp;
    struct timespec rx_ts;

    memset(&rx_ts, 0, sizeof(rx_ts));
    get_hw_timestamp(msg, &rx_ts);

    tp = (struct ether_testpacket*)msg->msg_iov->iov_base;

    /* copy info from testpacket */
    result->seq = tp->seq;
    result->interval_us = tp->interval_us;
    result->packet_size = tp->packet_size;

    /* calc diff between timestamp in testpacket hardware timestamp */
    timespec_diff(&tp->ts_tx_target, &rx_ts, &result->diff_ts);

    /* calc absolut to interval begin */
    result->abs_ns = rx_ts.tv_nsec % (tp->interval_us * 1000);

    /* calc dropped count and sequence error */
    check_sequence_num(tp->seq, &result->dropped, &result->seq_error);

    return 0;
}

static char *dump_json_test_packet(struct test_packet_result *result)
{
    json_t *j;
    char *str;

    j = json_pack("{sisisisisisisisb}",
                  "sequence", result->seq,
                  "delay_us", (result->diff_ts.tv_nsec/1000),
                  "delay_nsec", result->diff_ts.tv_nsec,
                  "delay_sec", result->diff_ts.tv_sec,
                  "abs_ns", result->abs_ns,
                  "interval_us", result->interval_us,
                  "packet_size", result->packet_size,
                  "dropped_packets", result->dropped,
                  "sequence_error", result->seq_error
    );

    str = json_dumps(j, JSON_COMPACT);
    json_decref(j);

    return str;
}

static int update_histogram(struct test_packet_result *result)
{
    int latency_usec = result->diff_ts.tv_nsec/1000;

    if (latency_usec > histogram.max || histogram.max == 0) {
        histogram.max = latency_usec;
    }
    if (latency_usec < histogram.min || histogram.min == 0) {
        histogram.min = latency_usec;
    }

    if (latency_usec < o_histogram) {
        histogram.array[latency_usec]++;
    } else {
        histogram.outliers++;
    }

    histogram.count++;

    return 0;
}

static char *dump_json_histogram(void)
{
    json_t *j, *a;
    char *s;
    int i;

    a = json_array();

    for (i = 0; i < o_histogram; i++) {
        json_t *integer;
        integer = json_integer(histogram.array[i]);
        json_array_append(a, integer);
        json_decref(integer);
    }

    j = json_pack("{sisisisiso?}",
                  "count", histogram.count,
                  "min", histogram.min,
                  "max", histogram.max,
                  "outliers", histogram.outliers,
                  "histogram", a
    );

    s = json_dumps(j, JSON_COMPACT);
    json_decref(j);

    return s;
}

static int handle_status_socket(int fd_socket, char *result_str)
{
    int rc = 0;

    int i;
    int sd;
    int activity;
    int new_socket;
    fd_set readfds;
    struct timeval timeout;

    /* Initialize the file descriptor set. */
    FD_ZERO(&readfds);
    FD_SET(fd_socket, &readfds);
    max_fd = fd_socket;

    /* Initialize the timeout data structure. */
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    /* add child sockets to set */
    for (i = 0 ; i < MAX_CLIENTS; i++) {
        sd = client_socket[i];
        if (sd > 0) {
            FD_SET(sd, &readfds);
        }

        if (sd > max_fd) {
            max_fd = sd;
        }
    }

    activity = select(max_fd + 1, &readfds, NULL, NULL, &timeout);
    if ((activity < 0) && (errno != EINTR)) {
        perror("select error");
    }

    /* check for new incoming connection */
    if (FD_ISSET(fd_socket, &readfds)) {
        if ((new_socket = accept(fd_socket, NULL, NULL)) < 0) {
            perror("accept");
            return 0;
        }


        /* todo check for max reached ... */
        for (i = 0 ; i < MAX_CLIENTS; i++) {
            if (client_socket[i] == 0) {
                client_socket[i] = new_socket;
                break;
            }
        }
    }

    if (result_str == NULL) {
        return -1;
    }

    /* handle all active connecitons */
    for (i = 0; i < MAX_CLIENTS; i++) {
        sd = client_socket[i];

        if (sd == 0) {
            continue;
        }

        if (FD_ISSET(sd, &readfds)) {
            char t[32];
            if (read(sd, t, 32) == 0) {
                close(sd);
                client_socket[i] = 0;
            }
        }

        if (write(sd, result_str, strlen(result_str) + 1) <= 0) {
            client_socket[i] = 0;
        }
    }

    return rc;
}

static int handle_msg(struct msghdr *msg, int fd_socket)
{
    int rc = 0;
    struct timespec rx_ts;
    struct test_packet_result result;
    char *result_str = NULL;

    memset(&rx_ts, 0, sizeof(rx_ts));
    get_hw_timestamp(msg, &rx_ts);

    /* build result message string */
    {
        switch (o_capture_ethertype) {
        case TEST_PACKET_ETHER_TYPE:
            handle_test_packet(msg, &result);
            result_str = dump_json_test_packet(&result);

            if (o_histogram) {
                update_histogram(&result);
            }
            break;
        default:
            printf("tbd ... other packet\n");
            break;
        }
    }

    if (o_verbose && result_str) {
        printf("%s\n", result_str);
    }

//    if (o_verbose) {
//        char *histogram_str = NULL;
//        histogram_str = dump_json_histogram();
//        printf("%s\n", histogram_str);
//        free(histogram_str);
//    }

    if (fd_socket != -1 && result_str) {
        rc = handle_status_socket(fd_socket, result_str);
    }

    if (result_str != NULL) {
        free(result_str);
    }

    return rc;
}

int get_own_eth_address(int fd, gchar *ifname, struct ether_addr *src_eth_addr)
{
    struct ifreq ifopts;

    /* determine own ethernet address */
    memset(&ifopts, 0, sizeof(struct ifreq));
    strncpy(ifopts.ifr_name, ifname, sizeof(ifopts.ifr_name));
    if (ioctl(fd, SIOCGIFHWADDR, &ifopts) < 0) {
        perror("ioctl");
        return -1;
    }

    memcpy(src_eth_addr, &ifopts.ifr_hwaddr.sa_data, ETH_ALEN);

    return 0;
}

int open_capture_interface(gchar *ifname)
{
    int rc;
    int fd;
    struct ifreq ifr;
    int opt;

    fd = socket(PF_PACKET, SOCK_RAW, htons(o_capture_ethertype));
    if (fd < 0) {
        perror("socket()");
        return -1;
    }

    /* Allow the socket to be reused */
    opt = 0;
    rc = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
    if (rc == -1) {
        perror("setsockopt() ... enable");
        close(fd);
        return -1;
    }

    /* configure timestamping */
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

    /* Enable timestamping */
    opt = 0;
    opt = SOF_TIMESTAMPING_RX_HARDWARE
          | SOF_TIMESTAMPING_RAW_HARDWARE
          | SOF_TIMESTAMPING_SYS_HARDWARE
          | SOF_TIMESTAMPING_SOFTWARE;
    rc = setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING, &opt, sizeof(opt));
    if (rc == -1) {
        perror("setsockopt() ... enable timestamp");
        return -1;
    }

    /* Bind to device */
    rc = setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, ifname, IFNAMSIZ-1);
    if (rc == -1) {
        perror("setsockopt() ... bind to device");
        close(fd);
        return -1;
    }

    return fd;
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


static gboolean parse_histogram_cb(const char *key, const char *value,
        gpointer user_data, GError **error)
{
    gchar *endPtr;
    (void)key;
    (void)user_data;
    (void)error;


    if (value == NULL) {
        o_histogram = HISTOGRAM_VALUES_MAX;
    } else {
        o_histogram = g_ascii_strtoull(value, &endPtr, 10);
        if (o_histogram > HISTOGRAM_VALUES_MAX) {
            o_histogram = HISTOGRAM_VALUES_MAX;
        }
    }

    return TRUE;
}


static GOptionEntry entries[] = {
    { "verbose",   'v', 0, G_OPTION_ARG_NONE,
            &o_verbose, "Be verbose", NULL },
    { "quiet",     'q', 0, G_OPTION_ARG_NONE,
            &o_quiet, "Suppress error messages", NULL },
    { "socket",    's', 0, G_OPTION_ARG_NONE,
            &o_socket, "Write packet results to socket", NULL },
    { "histogram", 'h', G_OPTION_FLAG_OPTIONAL_ARG , G_OPTION_ARG_CALLBACK,
            parse_histogram_cb, "Write packet histogram in JSON format", NULL},
    { "ethertype", 'e', 0, G_OPTION_ARG_INT,
            &o_capture_ethertype, "Set ethertype to filter"
            "(Default is 0x0808, ETH_P_ALL is 0x3)", NULL },
    { "rxfilter", 'f', 0, G_OPTION_ARG_CALLBACK,
            parse_rx_filter_cb, "Set hw rx filterfilter", NULL },
    { "ptp", 'p', 0, G_OPTION_ARG_CALLBACK,
            parse_rx_filter_cb, "Set hw rx filterfilter", NULL },
    { "version",   'V', 0, G_OPTION_ARG_NONE,
            &o_version, "Show version inforamtion and exit", NULL },
    { NULL, 0, 0, 0, NULL, NULL, NULL }
};

gint parse_command_line_options(gint *argc, char **argv)
{
    GError *error = NULL;
    GOptionContext *context;

    context = g_option_context_new("DEVICE - receive timestamped test packets");

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
        if (o_histogram) {
            char *histogram_str = NULL;
            histogram_str = dump_json_histogram();
            printf("%s\n", histogram_str);
            free(histogram_str);
        }
        exit(1);
    break;
    case SIGUSR1:
        if (o_histogram) {
            char *histogram_str = NULL;
            histogram_str = dump_json_histogram();
            printf("%s\n", histogram_str);
            free(histogram_str);
        }
    break;
    default:
    break;
    }
}


int main(int argc, char **argv)
{
    int rc;
    int fd;
    int fd_socket = -1;
    struct ether_addr *src_eth_addr = NULL;
    char *ifname = NULL;
    sigset_t sigset;

    parse_command_line_options(&argc, argv);

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
        src_eth_addr = &my_eth_addr;
    }

    if (o_socket) {
        fd_socket = open_server_socket(socket_path);
    }

    sigemptyset(&sigset);
//	sigaddset(&sigset, SIGALARM);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGUSR1, signal_handler);

    while (1) {
        struct msghdr msg;
        struct iovec iov;
        struct sockaddr_in host_address;
        unsigned char buffer[BUF_SIZE];
        char control[BUF_CONTROL_SIZE];

        /* recvmsg header structure */
        iov.iov_base = buffer;
        iov.iov_len = BUF_SIZE;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_name = &host_address;
        msg.msg_namelen = sizeof(struct sockaddr_in);
        msg.msg_control = control;
        msg.msg_controllen = BUF_CONTROL_SIZE;

        rc = receive_msg(fd, src_eth_addr, &msg);
        if (rc > 0) {
            handle_msg(&msg, fd_socket);
        }
    }

    close(fd);

    return 0;
}
