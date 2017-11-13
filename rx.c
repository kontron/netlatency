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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <fcntl.h>
#include <sys/socket.h>

#include <linux/net_tstamp.h>
#include <linux/sockios.h>

#include <glib.h>
#include <glib/gprintf.h>

#include "stats.h"
#include "data.h"
#include "domain_socket.h"

static gchar *help_description = NULL;
static gint o_verbose = 0;
static gint o_quiet = 0;
static gint o_version = 0;
static gint o_socket = 0;
static gint o_capture_ethertype = 0x0808;
static gint o_rx_filter = HWTSTAMP_FILTER_ALL;
static gint o_ptp_mode = FALSE;


//proto 88f7
//myaddr 0
//filter L2_V2_EVENT

static void get_hw_timestamp(struct msghdr *msg, struct timespec *ts)
{
	struct cmsghdr *cmsg;

	struct scm_timestamping {
		struct timespec ts[3];
	};

	for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg,cmsg)) {
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

struct stats stats;

#define MAX_CLIENTS 2
static int client_socket[MAX_CLIENTS];
static int max_fd;

static int handle_msg(struct msghdr *msg, int fd_socket)
{
	struct ether_testpacket *tp;
	struct ethhdr *ethhdr;
	struct timespec rx_ts;
	struct timespec diff_ts;

	ethhdr = (struct ethhdr*)msg->msg_iov->iov_base;
	tp = (struct ether_testpacket*)msg->msg_iov->iov_base;

	memset(&rx_ts, 0, sizeof(rx_ts));
	get_hw_timestamp(msg, &rx_ts);

	memset(&rx_ts, 0, sizeof(rx_ts));
	get_hw_timestamp(msg, &rx_ts);

	/* calc diff between timestamp in testpacket tp and received packet rx_ts */
	timespec_diff(&tp->ts, &rx_ts, &diff_ts);

	/* calc stats wtih stored previous packages */
	calc_stats(&rx_ts, &stats, tp->interval_us);

	char str[1024];
	memset(str, 0, sizeof(str));

	/* build result message string */
	{
		switch (o_capture_ethertype) {
#if 0
		case 0x00808:
			snprintf(str, sizeof(str), "TS(l): %lld.%.06ld; TS(r): %lld.%.06ld; SEQ: %-d; DIFF: %ld; MEAN: %ld; MAX: %ld;\n",
				(long long)rx_ts.tv_sec,
				(rx_ts.tv_nsec / 1000),
				(long long)tp->ts.tv_sec,
				(tp->ts.tv_nsec / 1000),
				tp->seq,
				(stats.diff.tv_nsec / 1000),
				(stats.mean.tv_nsec / 1000),
				(stats.max.tv_nsec / 1000)
			);
			break;
#endif
		case 0x00808:
			snprintf(str, sizeof(str), "TS(rx): %lld.%.06ld; TS(tx): %lld.%.06ld; SEQ: %-d; DIFF: %ld;\n",
				(long long)rx_ts.tv_sec, (rx_ts.tv_nsec / 1000),
				(long long)tp->ts.tv_sec, (tp->ts.tv_nsec / 1000),
				tp->seq,
				(diff_ts.tv_nsec / 1000)
			);
			break;
		default:
			snprintf(str, sizeof(str), "TS(l): %lld.%.06ld;\n",
				(long long)rx_ts.tv_sec,
				(rx_ts.tv_nsec / 1000)
			);
			break;
		}
	}

	if (o_verbose) {
		printf("src: %02x:%02x:%02x:%02x:%02x:%02x; ",
				ethhdr->h_source[0],
				ethhdr->h_source[1],
				ethhdr->h_source[2],
				ethhdr->h_source[3],
				ethhdr->h_source[4],
				ethhdr->h_source[5]);
		printf("dst: %02x:%02x:%02x:%02x:%02x:%02x; ",
				ethhdr->h_dest[0],
				ethhdr->h_dest[1],
				ethhdr->h_dest[2],
				ethhdr->h_dest[3],
				ethhdr->h_dest[4],
				ethhdr->h_dest[5]);

		printf("proto: 0x%04x, ", ntohs(ethhdr->h_proto));
		printf("%s", str);
	}

	if (fd_socket != -1) {
		int i;
		int sd;
		int activity;
		int new_socket;
		fd_set readfds;

		FD_ZERO(&readfds);
		FD_SET(fd_socket, &readfds);
		max_fd = fd_socket;

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

		struct timeval waitd = {0, 0};
		activity = select(max_fd + 1 , &readfds , NULL , NULL , &waitd);
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

		/* handle all active connecitons */
		for (i = 0; i < MAX_CLIENTS; i++) {
			sd = client_socket[i];
			if (sd == 0) {
				continue;
			}
			if (FD_ISSET( sd , &readfds)) {
				char t[32];
				if (read(sd ,t, 32) == 0) {
					close(sd);
					client_socket[i] = 0;
				}
			}

			if (write(sd, str, strlen(str)) <= 0) {
				client_socket[i] = 0;
			}
		}
	}

	return 0;
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
	int sockopt;

	fd = socket(PF_PACKET, SOCK_RAW, htons(o_capture_ethertype));
	if (fd < 0) {
		perror("socket()");
		return -1;
	}

	if (0) {
		/* Set interface to promiscuous mode - do we need to do this every time? */
		/* no .. check if promiscuous mode was set before */
		strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);
		ioctl(fd, SIOCGIFFLAGS, &ifr);
		ifr.ifr_flags |= IFF_PROMISC;
		ioctl(fd, SIOCSIFFLAGS, &ifr);
	}

	{
		sockopt = 0;
		/* Allow the socket to be reused - incase connection is closed prematurely */
		rc = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof sockopt);
		if (rc == -1) {
			perror("setsockopt() ... enable");
			close(fd);
			return -1;
		}
	}

	/* configure timestamping */
	{
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
	}

	/* Enable timestamping */
	{
		sockopt = 0;
		sockopt = SOF_TIMESTAMPING_RX_HARDWARE
				| SOF_TIMESTAMPING_RAW_HARDWARE
				| SOF_TIMESTAMPING_SYS_HARDWARE
				| SOF_TIMESTAMPING_SOFTWARE;
		rc = setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING, &sockopt, sizeof(sockopt));
		if (rc == -1) {
			perror("setsockopt() ... enable timestamp");
			return -1;
		}
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

	for (i = 0; filter_map[i].name != NULL; i++){
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
	{ "socket",    's', 0, G_OPTION_ARG_NONE,
			&o_socket, "Write stats to domain socket", NULL },
	{ "ethertype", 'e', 0, G_OPTION_ARG_INT,
			&o_capture_ethertype, "Set ethertype to filter. Default is 0x0808. ETH_P_ALL is 0x3", NULL },
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


int main(int argc, char **argv)
{
	int rc;
	int fd;
	int fd_socket = -1;
	struct ether_addr *src_eth_addr = NULL;
	char *ifname = NULL;

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

	memset(&stats, 0, sizeof(struct stats));
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
