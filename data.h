#ifndef __DATA_H__
#define __DATA_H__

#include <net/ethernet.h>
#include <netinet/ether.h>

#define TEST_PACKET_ETHER_TYPE 0x0808

struct ether_testpacket {
	struct ether_header hdr;
	struct timespec ts;
	uint32_t seq;
	uint32_t interval_us;
	uint32_t packet_size;
};

#endif
