
#include <linux/net_tstamp.h>
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

#include "timestamps.h"

#include <linux/errqueue.h>

int configure_tx_timestamp(int fd, char* ifname)
{
	int opt = SOF_TIMESTAMPING_TX_SCHED |
				SOF_TIMESTAMPING_TX_SOFTWARE |
				SOF_TIMESTAMPING_TX_ACK |
				SOF_TIMESTAMPING_SOFTWARE |
				SOF_TIMESTAMPING_OPT_CMSG |
				SOF_TIMESTAMPING_OPT_ID |
				SOF_TIMESTAMPING_TX_HARDWARE;

	//int opt = SOF_TIMESTAMPING_TX_HARDWARE;

    (void)ifname;
	/* configure timestamping */
	{
		struct hwtstamp_config config;
		struct ifreq ifr;

		config.flags = 0;
		config.tx_type = HWTSTAMP_TX_ON;
		config.rx_filter = HWTSTAMP_FILTER_ALL;

		snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifname);
		ifr.ifr_data = (caddr_t)&config;
		if (ioctl(fd, SIOCSHWTSTAMP, &ifr)) {
			perror("ioctl() ... configure timestamping\n");
			return -1;
		}
	}

	if (setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING,
		       (char *) &opt, sizeof(opt))) {
		perror("setsockopt timestamping");
	}

	return 0;
}

static struct timespec ts_prev;
static void __print_timestamp(const char *name, struct timespec *cur,
			      uint32_t key, int payload_len)
{
	if (!(cur->tv_sec | cur->tv_nsec))
		return;

	fprintf(stderr, "  %s: %lu s %lu us (seq=%u, len=%u)",
			name, cur->tv_sec, cur->tv_nsec / 1000,
			key, payload_len);

	if ((ts_prev.tv_sec | ts_prev.tv_nsec)) {
		int64_t cur_ms, prev_ms;

		cur_ms = (long) cur->tv_sec * 1000 * 1000;
		cur_ms += cur->tv_nsec / 1000;

		prev_ms = (long) ts_prev.tv_sec * 1000 * 1000;
		prev_ms += ts_prev.tv_nsec / 1000;

		fprintf(stderr, "  (%+" PRId64 " us)", cur_ms - prev_ms);
	}

	ts_prev = *cur;
	fprintf(stderr, "\n");
}

static void print_timestamp(struct scm_timestamping *tss, int tstype,
			    int tskey, int payload_len)
{
	const char *tsname;

	switch (tstype) {
	case SCM_TSTAMP_SCHED:
		tsname = "  ENQ";
		break;
	case SCM_TSTAMP_SND:
		tsname = "  SND";
		break;
	case SCM_TSTAMP_ACK:
		tsname = "  ACK";
		break;
	default:
		printf("unknown timestamp type: %u", tstype);
		return;
	}
	__print_timestamp(tsname, &tss->ts[0], tskey, payload_len);
}

/* TODO: convert to check_and_print payload once API is stable */
static void print_payload(char *data, int len)
{
	int i;

	if (!len)
		return;

	if (len > 70)
		len = 70;

	fprintf(stderr, "payload: ");
	for (i = 0; i < len; i++)
		fprintf(stderr, "%02hhx ", data[i]);
	fprintf(stderr, "\n");
}

static int __recv_errmsg_cmsg(struct msghdr *msg, int payload_len, char *data)
{
	struct sock_extended_err *serr = NULL;
	struct scm_timestamping *tss = NULL;
	struct cmsghdr *cm;
	int batch = 0;
	int ret = 0;

	(void)payload_len;

	for (cm = CMSG_FIRSTHDR(msg); cm && cm->cmsg_len; cm = CMSG_NXTHDR(msg, cm)) {

		if (cm->cmsg_level == SOL_PACKET &&
		    cm->cmsg_type == PACKET_TX_TIMESTAMP) {
			serr = (void *) CMSG_DATA(cm);

#if 0
			if (serr->ee_info != SCM_TSTAMP_SND) {
				serr = NULL;
				continue;
			}
#endif

			printf("err tstype=%d, tskey=%d errno=%d origin=%d\n",
					serr->ee_info, serr->ee_data, serr->ee_errno, serr->ee_origin);
		} else if (cm->cmsg_level == SOL_SOCKET &&
		    cm->cmsg_type == SO_TIMESTAMPING) {
			tss = (void *) CMSG_DATA(cm);

			printf("timestamp..\n");
#if 0
			printf("ts2 %ld.%.06ld %ld.%.06ld %ld.%.06ld\n",
				tss->ts[0].tv_sec,
				tss->ts[0].tv_nsec/1000,
				tss->ts[1].tv_sec,
				tss->ts[1].tv_nsec/1000,
				tss->ts[2].tv_sec,
				tss->ts[2].tv_nsec/1000);
#endif

		} else {
			fprintf(stderr, "unknown cmsg %d,%d\n",
					cm->cmsg_level, cm->cmsg_type);
			serr = NULL;
			tss = NULL;
		}

		if (serr && tss) {
			print_timestamp(tss, serr->ee_info, serr->ee_data,
					payload_len);
			fprintf(stderr, "seq=%d\n", data[32]);
			(void)print_payload;
			//print_payload(data, 70);
			ret = serr->ee_info == SCM_TSTAMP_SND;
			serr = NULL;
			tss = NULL;
			batch++;
		}
	}

	if (batch > 1)
		fprintf(stderr, "batched %d timestamps\n", batch);
	return ret;
}

#include <poll.h>
void __poll(int fd)
{
	struct pollfd pollfd;
	int ret;

	memset(&pollfd, 0, sizeof(pollfd));
	pollfd.fd = fd;
	ret = poll(&pollfd, 1, 100);
	if (ret != 1)
		perror("poll");
}


#define RX_PAYLOAD_LEN 1024
int get_tx_timestamp(int fd)
{
	static char ctrl[1024 /* overprovision*/];
	static struct msghdr msg;
	struct iovec entry;
	static char *data;
	int ret = 0;

	data = malloc(RX_PAYLOAD_LEN);
	if (!data)
		perror("malloc");

	memset(&msg, 0, sizeof(msg));
	memset(&entry, 0, sizeof(entry));
	memset(ctrl, 0, sizeof(ctrl));

	entry.iov_base = data;
	entry.iov_len = RX_PAYLOAD_LEN;
	msg.msg_iov = &entry;
	msg.msg_iovlen = 1;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_control = ctrl;
	msg.msg_controllen = sizeof(ctrl);

	ret = recvmsg(fd, &msg, MSG_ERRQUEUE);
	if (ret == -1 && errno != EAGAIN)
		perror("recvmsg");

	if (ret >= 0) {
		ret = __recv_errmsg_cmsg(&msg, ret, data);
	}

	free(data);
	return ret;
}
