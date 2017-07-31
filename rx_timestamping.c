#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <arpa/inet.h>

#include <linux/net_tstamp.h>
#include <linux/sockios.h>

static void print_timestamp(struct msghdr* msg)
{
	struct cmsghdr *cmsg;

	for(cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg,cmsg) ) {
		struct timespec *ts = NULL;

		if( cmsg->cmsg_level != SOL_SOCKET ) {
			continue;
		}

		switch (cmsg->cmsg_type) {
		case SO_TIMESTAMPNS:
			ts = (struct timespec*) CMSG_DATA(cmsg);
			printf("  SO_TIMESTAMPNS: ");
			break;
		case SO_TIMESTAMPING:
			ts = (struct timespec*) CMSG_DATA(cmsg);
			printf("  SO_TIMESTAMPING: ");
			break;
		default:
			printf("  cmsg_type=%d\n", cmsg->cmsg_type);
			/* Ignore other cmsg options */
			break;
		}


		if (ts) {
			printf("%lld.%.9ld\n", (long long)ts->tv_sec, ts->tv_nsec);
		}
	}
}


#define BUF_SIZE 10*1024
static int do_receive(int sockfd)
{
	struct msghdr msg;
	struct iovec iov;
	struct sockaddr_in host_address;
	char buffer[2048];
	char control[1024];
	int n;

	/* recvmsg header structure */
	iov.iov_base = buffer;
	iov.iov_len = 2048;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_name = &host_address;
	msg.msg_namelen = sizeof(struct sockaddr_in);
	msg.msg_control = control;
	msg.msg_controllen = 1024;

	/* block for message */
	n = recvmsg(sockfd, &msg, 0);
	if( !n && errno == EAGAIN ) {
		return 0;
	}

	printf("Packet - %d bytes\t", n);
	print_timestamp(&msg);

	return n;
}

int main(int argc, char **argv)
{
	int rv;
	int sockfd;
	struct ifreq ifopts;

	char *ifname = "eth0";

	(void)argc;
	(void)argv;

	sockfd = socket(PF_PACKET, SOCK_RAW, htons(0x800));
	//sockfd = socket(PF_PACKET, SOCK_RAW, AF_PACKET);
	if (sockfd < 0) {
		perror("socket()");
		return -1;
	}

	/* Set interface to promiscuous mode - do we need to do this every time? */
	strncpy(ifopts.ifr_name, ifname, IFNAMSIZ-1);
	ioctl(sockfd, SIOCGIFFLAGS, &ifopts);
	ifopts.ifr_flags |= IFF_PROMISC;
	ioctl(sockfd, SIOCSIFFLAGS, &ifopts);

#if 0
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
#endif

	/* Enable timestamping */
	{
		int enable = 1;
		enable = SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE
				| SOF_TIMESTAMPING_SYS_HARDWARE | SOF_TIMESTAMPING_SOFTWARE;
		rv = setsockopt(sockfd, SOL_SOCKET, SO_TIMESTAMPING, &enable, sizeof(int));
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
		do_receive(sockfd);
	}

	close(sockfd);

	return 0;
}
