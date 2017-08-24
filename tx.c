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
#include <net/ethernet.h>
#include <netinet/in.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

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
	return (NULL);
}

eth_t *eth_open(const char *device)
{
	eth_t *e;
	int n;

	if ((e = calloc(1, sizeof(*e))) != NULL) {
		if ((e->fd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) < 0) {
			return (eth_close(e));
		}
#ifdef SO_BROADCAST
		n = 1;
		if (setsockopt(e->fd, SOL_SOCKET, SO_BROADCAST, &n, sizeof(n)) < 0) {
			return (eth_close(e));
		}
#endif
		strncpy(e->ifr.ifr_name, device, sizeof(e->ifr.ifr_name));
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

int eth_get(eth_t *e, struct ether_addr *ea)
{
	if (ioctl(e->fd, SIOCGIFHWADDR, &e->ifr) < 0) {
		return -1;
	}

	memcpy(ea, &e->ifr.ifr_hwaddr.sa_data, ETH_ALEN);
	return 0;
}

int main(int argc, char **argv)
{
	int rv = 0;

	eth_t *eth;
	uint8_t buf[1024];
	size_t idx = 0;

	struct timespec ts;

	if (argc < 2) {
		return -1;
	}

	eth = eth_open(argv[1]);

	buf[idx++] = 0xff;
	buf[idx++] = 0xff;
	buf[idx++] = 0xff;
	buf[idx++] = 0xff;
	buf[idx++] = 0xff;
	buf[idx++] = 0xff;

	buf[idx++] = 0xaa;
	buf[idx++] = 0xbb;
	buf[idx++] = 0xcc;
	buf[idx++] = 0xdd;
	buf[idx++] = 0xee;
	buf[idx++] = 0xff;

	/* ethertype */
	buf[idx++] = 0x08;
	buf[idx++] = 0x08;

	clock_gettime( CLOCK_REALTIME, &ts);

	printf("%lld.%.9ld\n",
			(long long)ts.tv_sec, ts.tv_nsec);
	printf("%lx.%x\n",
			(long long)ts.tv_sec, ts.tv_nsec);
	memcpy(buf+idx, &ts, sizeof(struct timespec));
	idx += sizeof(struct timespec);


	eth_send(eth, buf, 64);

	return rv;
}
