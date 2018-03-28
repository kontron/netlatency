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

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
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
#include "domain_socket.h"
#include "timer.h"

#ifndef VERSION
#define VERSION "dev"
#endif

static gchar *help_description = NULL;
static gint o_capture_ethertype = TEST_PACKET_ETHER_TYPE;
static gint o_count = 0;
static gint o_ptp_mode = FALSE;
static gint o_quiet = 0;
static gint o_rx_filter = HWTSTAMP_FILTER_ALL;
static gint o_socket = 0;
static gint o_verbose = 0;
static gint o_version = 0;


static gint do_shutdown = 0;

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
        gboolean *sequence_error, gboolean reset_last_seq)
{
    int rc = 0;
    static unsigned long last_seq = 0;

    if (reset_last_seq) {
        last_seq = 0;
    }

    if (last_seq == 0) {
        last_seq = seq;
        *dropped_packets = 0;
        *sequence_error = 0;
        return 0;
    }

    *dropped_packets = seq - last_seq - 1;
    *sequence_error = seq <= last_seq;

    if (*dropped_packets || *sequence_error) {
        rc = 1;
    }

    last_seq = seq;

    return rc;
}

struct test_packet_result {
    uint32_t seq;
    uint32_t interval_us;
    uint32_t packet_size;

    struct timespec tx_user_ts;
    struct timespec rx_hw_ts;
    struct timespec rx_user_ts;
    struct timespec latency_ts;
    uint32_t abs_ns;

    long dropped;
    gboolean seq_error;
};

static int handle_test_packet(struct msghdr *msg,
        struct test_packet_result *result)
{
    struct ether_testpacket *tp;

    get_hw_timestamp(msg, &result->rx_hw_ts);

    tp = (struct ether_testpacket*)msg->msg_iov->iov_base;

    /* copy info from testpacket */
    result->seq = tp->seq;
    result->interval_us = tp->interval_us;
    result->packet_size = tp->packet_size;
    memcpy(&result->tx_user_ts, &tp->ts_tx, sizeof(struct timespec));

    /* calc diff between timestamp in testpacket hardware timestamp */
    timespec_diff(&tp->ts_tx_target, &result->rx_hw_ts, &result->latency_ts);

    /* calc absolut to interval begin */
    if (tp->interval_us != 0) {
        result->abs_ns = result->rx_hw_ts.tv_nsec % (tp->interval_us * 1000);
    }

    /* calc dropped count and sequence error */
    check_sequence_num(tp->seq, &result->dropped, &result->seq_error, FALSE);

    return 0;
}

static char *dump_json_test_packet(struct test_packet_result *result)
{
    json_t *j;
    char *s;
    char *s_tx_user;
    char *s_rx_hw;
    char *s_rx_user;

    s_tx_user = timespec_to_iso_string(&result->tx_user_ts);
    s_rx_hw = timespec_to_iso_string(&result->rx_hw_ts);
    s_rx_user = timespec_to_iso_string(&result->rx_user_ts);

    j = json_pack("{sss{sisissssss}}",
                  "type", "rx-packet",
                  "object",
                  "sequence-number", result->seq,
                  "packet-size", result->packet_size,
                  "tx-user-timestamp", s_tx_user,
                  "rx-hw-timestamp", s_rx_hw,
                  "rx-user-timestamp", s_rx_user
    );

    g_free(s_tx_user);
    g_free(s_rx_hw);
    g_free(s_rx_user);

    s = json_dumps(j, JSON_COMPACT);
    json_decref(j);

    return s;
}

static char *dump_json_error(struct test_packet_result *result)
{
    char *s = NULL;
    json_t *j;

    j = json_pack("{sss{sisb}}",
                  "type", "rx-error",
                  "object",
                  "dropped-packets", result->dropped,
                  "sequence-error", result->seq_error
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
    struct test_packet_result result;
    char *json_rx_packet_str = NULL;
    char *json_rx_error_str = NULL;

    clock_gettime(CLOCK_REALTIME, &result.rx_user_ts);

    /* build result message string */
    {
        switch (o_capture_ethertype) {
        case TEST_PACKET_ETHER_TYPE:
            handle_test_packet(msg, &result);


            if (result.dropped || result.seq_error) {
                json_rx_error_str = dump_json_error(&result);
            }
            if (json_rx_error_str) {
                if (!o_quiet) {
                    printf("%s\n", json_rx_error_str);
                    fflush(stdout);
                }
                free(json_rx_error_str);
            }

            json_rx_packet_str = dump_json_test_packet(&result);
            if (json_rx_packet_str) {
                if (!o_quiet) {
                    printf("%s\n", json_rx_packet_str);
                    fflush(stdout);
                }

                if (fd_socket != -1) {
                    rc = handle_status_socket(fd_socket, json_rx_packet_str);
                }
                free(json_rx_packet_str);
            }

            break;
        default:
            printf("tbd ... other packet\n");
            break;
        }
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
    struct sockaddr_ll sock_address;

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
    memset(&sock_address, 0, sizeof(sock_address));
    sock_address.sll_family = PF_PACKET;
    sock_address.sll_protocol = htons(ETH_P_ALL);
    sock_address.sll_ifindex = if_nametoindex(ifname);

    if (bind(fd, (struct sockaddr*) &sock_address, sizeof(sock_address)) < 0) {
        perror("bind failed\n");
        close(fd);
        return -4;
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


static GOptionEntry entries[] = {
    { "verbose",   'v', 0, G_OPTION_ARG_NONE,
            &o_verbose, "Be verbose", NULL },
    { "quiet",     'q', 0, G_OPTION_ARG_NONE,
            &o_quiet, "Suppress error messages", NULL },
    { "count",    'c', 0, G_OPTION_ARG_INT,
            &o_count,
            "Receive packet count", NULL },
    { "socket",    's', 0, G_OPTION_ARG_NONE,
            &o_socket, "Write packet results to socket", NULL },
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
    int fd_socket = -1;
    struct ether_addr *src_eth_addr = NULL;
    char *ifname = NULL;
    sigset_t sigset;

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


    gint64 count = 0;
    while (!do_shutdown) {
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

        count++;
        if (o_count && count >= o_count) {
            break;
        }
    }

    close(fd);

    return 0;
}


int main(int argc, char **argv)
{
    return real_main(argc, argv);
}
