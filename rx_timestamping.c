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


struct scm_timestamping {
	struct timespec ts[3];
};

static void print_msg(struct msghdr* msg)
{
	struct cmsghdr *cmsg;

	for(cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg,cmsg) ) {
		struct scm_timestamping* scm_ts = NULL;

		if( cmsg->cmsg_level != SOL_SOCKET ) {
			continue;
		}

		switch (cmsg->cmsg_type) {
		case SO_TIMESTAMP:
			printf("SO_TIMESTAMP: ");
			break;
		case SO_TIMESTAMPNS:
			printf("SO_TIMESTAMPNS: ");
			break;
		case SO_TIMESTAMPING:
			scm_ts = (struct scm_timestamping*) CMSG_DATA(cmsg);
			printf("SO_TIMESTAMPING: ");
			break;
		default:
			printf("cmsg_type=%d", cmsg->cmsg_type);
			/* Ignore other cmsg options */
			break;
		}

		if (scm_ts) {
			printf("%lld.%.9ld, %lld.%.9ld, %lld.%.9ld",
					(long long)scm_ts->ts[0].tv_sec, scm_ts->ts[0].tv_nsec,
					(long long)scm_ts->ts[1].tv_sec, scm_ts->ts[1].tv_nsec,
					(long long)scm_ts->ts[2].tv_sec, scm_ts->ts[2].tv_nsec
					);
		}
	}
}


#define BUF_SIZE 10*1024
#define BUF_CONTROL_SIZE 1024
static int receive_msg(int sockfd)
{
	struct msghdr msg;
	struct iovec iov;
	struct sockaddr_in host_address;
	unsigned char buffer[BUF_SIZE];
	char control[BUF_CONTROL_SIZE];
	int n;

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
	n = recvmsg(sockfd, &msg, 0);
	if( !n && errno == EAGAIN ) {
		return 0;
	}

	struct ethhdr *ethhdr;

	ethhdr = (struct ethhdr*)buffer;

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

	print_msg(&msg);
	printf("\n");

	return n;
}

void usage(char **argv){
	printf("%s DEVICE\n", argv[0]);
}


int main(int argc, char **argv)
{
	int rv;
	int sockfd;
	struct ifreq ifopts;

	char *ifname = NULL;

	if (argc < 2) {
		usage(argv);
		return -1;
	}

	ifname = argv[1];

	sockfd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_IP));
	//sockfd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if (sockfd < 0) {
		perror("socket()");
		return -1;
	}

	if (0) {
		/* Set interface to promiscuous mode - do we need to do this every time? */
		/* no .. check if promiscuous mode was set before */
		strncpy(ifopts.ifr_name, ifname, IFNAMSIZ-1);
		ioctl(sockfd, SIOCGIFFLAGS, &ifopts);
		ifopts.ifr_flags |= IFF_PROMISC;
		ioctl(sockfd, SIOCSIFFLAGS, &ifopts);
	}

	{
		int sockopt;
		/* Allow the socket to be reused - incase connection is closed prematurely */
		rv = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof sockopt);
		if (rv == -1) {
			perror("setsockopt() ... enable");
			close(sockfd);
			exit(EXIT_FAILURE);
		}
	}

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
		if (ioctl(sockfd, SIOCSHWTSTAMP, &ifr)) {
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
		rv = setsockopt(sockfd, SOL_SOCKET, SO_TIMESTAMPING, &optval, sizeof(int));
		if (rv == -1) {
			perror("setsockopt() ... enable timestamp");
		}
	}

	/* Bind to device */
	setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE, ifname, IFNAMSIZ-1);
	if (rv == -1) {
		perror("setsockopt() ... bind to device");
		close(sockfd);
		exit(EXIT_FAILURE);
	}

	printf("start receiving ... \n");

	while (1) {
		receive_msg(sockfd);
	}

	close(sockfd);

	return 0;
}
