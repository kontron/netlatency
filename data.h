#ifndef __DATA_H__
#define __DATA_H__

#include <net/ethernet.h>
#include <netinet/ether.h>

struct ether_testpacket {
	struct ether_header hdr;
	struct timespec ts;
	uint32_t seq;

};

void calc_stats(struct timespec *ts, struct stats *stats);

#endif
