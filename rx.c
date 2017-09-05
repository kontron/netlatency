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

#include <linux/net_tstamp.h>
#include <linux/sockios.h>

#include <glib.h>
#include <glib/gprintf.h>

#include "stats.h"
#include "data.h"

static gchar *help_description = NULL;
static gint o_verbose = 0;
static gint o_quiet = 0;
static gint o_version = 0;

static void get_hw_timestamp(struct msghdr *msg, struct timespec *ts)
{
	struct cmsghdr *cmsg;

	struct scm_timestamping {
		struct timespec ts[3];
	};


	for(cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg,cmsg) ) {
		struct scm_timestamping* scm_ts = NULL;

		if( cmsg->cmsg_level != SOL_SOCKET ) {
			continue;
		}

		switch (cmsg->cmsg_type) {
		case SO_TIMESTAMP:
			//printf("SO_TIMESTAMP: ");
			break;
		case SO_TIMESTAMPNS:
			//printf("SO_TIMESTAMPNS: ");
			break;
		case SO_TIMESTAMPING:
			scm_ts = (struct scm_timestamping*) CMSG_DATA(cmsg);
			//printf("SO_TIMESTAMPING: ");
			memcpy(ts, &scm_ts->ts[2], sizeof(struct timespec));
			break;
		default:
			printf("cmsg_type=%d", cmsg->cmsg_type);
			/* Ignore other cmsg options */
			break;
		}
	}
}


struct stats stats;

#define BUF_SIZE 10*1024
#define BUF_CONTROL_SIZE 1024
static int receive_msg(int fd, struct ether_addr *my_eth_addr)
{
	struct msghdr msg;
	struct iovec iov;
	struct sockaddr_in host_address;
	unsigned char buffer[BUF_SIZE];
	char control[BUF_CONTROL_SIZE];
	int n;
	struct timespec ts;

	/* recvmsg header structure */
	iov.iov_base = buffer;
	iov.iov_len = BUF_SIZE;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_name = &host_address;
	msg.msg_namelen = sizeof(struct sockaddr_in);
	msg.msg_control = control;
	msg.msg_controllen = BUF_CONTROL_SIZE;

	/* block for message */
	n = recvmsg(fd, &msg, 0);
	if ( !n && errno == EAGAIN ) {
		return 0;
	}

	struct ether_testpacket *tp;
	struct ethhdr *ethhdr;
	ethhdr = (struct ethhdr*)buffer;
	tp = (struct ether_testpacket*)buffer;


	if (my_eth_addr != NULL) {
		/* filter for own ether packets */
		if (memcmp(my_eth_addr->ether_addr_octet, tp->hdr.ether_dhost, ETH_ALEN)) {
		//if (memcmp(my_eth_addr->ether_addr_octet, ethhdr->h_dest, ETH_ALEN)) {
			return -1;
		}
	}

	if (o_verbose) {
		printf("src: %02x:%02x:%02x:%02x:%02x:%02x, ",
				ethhdr->h_source[0],
				ethhdr->h_source[1],
				ethhdr->h_source[2],
				ethhdr->h_source[3],
				ethhdr->h_source[4],
				ethhdr->h_source[5]);
		printf("dst: %02x:%02x:%02x:%02x:%02x:%02x, ",
				ethhdr->h_dest[0],
				ethhdr->h_dest[1],
				ethhdr->h_dest[2],
				ethhdr->h_dest[3],
				ethhdr->h_dest[4],
				ethhdr->h_dest[5]);

		printf("proto: 0x%04x, ", ntohs(ethhdr->h_proto));
	}

	get_hw_timestamp(&msg, &ts);
	calc_stats(&ts, &stats);

	printf("SEQ %4d ", tp->seq);
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

	return n;
}

void usage(void)
{
	g_printf("%s", help_description);
}

static GOptionEntry entries[] = {
	{ "verbose",   'v', 0, G_OPTION_ARG_NONE,
			&o_verbose, "Be verbose", NULL },
	{ "quiet",     'q', 0, G_OPTION_ARG_NONE,
			&o_quiet, "Suppress error messages", NULL },
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
	int rv;

	int fd;
	struct ifreq ifopts;
	struct ether_addr src_eth_addr;

	char *ifname = NULL;

	parse_command_line_options(&argc, argv);

	if (argc < 2) {
		usage();
		return -1;
	}

	ifname = argv[1];

	//fd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_IP));
	//fd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	fd = socket(PF_PACKET, SOCK_RAW, htons(0x0808));
	if (fd < 0) {
		perror("socket()");
		return -1;
	}

	/* determine own ethernet address */
	memset(&ifopts, 0, sizeof(struct ifreq));
	strncpy(ifopts.ifr_name, argv[1], sizeof(ifopts.ifr_name));
	if (ioctl(fd, SIOCGIFHWADDR, &ifopts) < 0) {
		perror("ioctl");
		return -1;
	}

	memcpy(&src_eth_addr, &ifopts.ifr_hwaddr.sa_data, ETH_ALEN);

	if (0) {
		/* Set interface to promiscuous mode - do we need to do this every time? */
		/* no .. check if promiscuous mode was set before */
		strncpy(ifopts.ifr_name, ifname, IFNAMSIZ-1);
		ioctl(fd, SIOCGIFFLAGS, &ifopts);
		ifopts.ifr_flags |= IFF_PROMISC;
		ioctl(fd, SIOCSIFFLAGS, &ifopts);
	}

	{
		int sockopt;
		/* Allow the socket to be reused - incase connection is closed prematurely */
		rv = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof sockopt);
		if (rv == -1) {
			perror("setsockopt() ... enable");
			close(fd);
			exit(EXIT_FAILURE);
		}
	}

	/* configure timestamping */
	{
		struct ifreq ifr;
		struct hwtstamp_config config;

		config.flags = 0;
		config.tx_type = HWTSTAMP_TX_ON;
		config.rx_filter = HWTSTAMP_FILTER_ALL;
		if (config.tx_type < 0 || config.rx_filter < 0) {
			return 2;
		}

		snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifname);
		ifr.ifr_data = (caddr_t)&config;
		if (ioctl(fd, SIOCSHWTSTAMP, &ifr)) {
			perror("ioctl()\n");
			return 1;
		}
	}

	/* Enable timestamping */
	{
		int optval = 1;
		optval = SOF_TIMESTAMPING_RX_HARDWARE
				| SOF_TIMESTAMPING_RAW_HARDWARE
				| SOF_TIMESTAMPING_SYS_HARDWARE
				| SOF_TIMESTAMPING_SOFTWARE;
		rv = setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING, &optval, sizeof(int));
		if (rv == -1) {
			perror("setsockopt() ... enable timestamp");
		}
	}

	/* Bind to device */
	setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, ifname, IFNAMSIZ-1);
	if (rv == -1) {
		perror("setsockopt() ... bind to device");
		close(fd);
		exit(EXIT_FAILURE);
	}


	memset(&stats, 0, sizeof(struct stats));
	while (1) {
		receive_msg(fd, &src_eth_addr);
	}

	close(fd);

	return 0;
}
