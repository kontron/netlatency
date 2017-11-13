#ifndef __TIMESTAMPS_H__
#define __TIMESTAMPS_H__

typedef struct eth_handle eth_t;

int configure_tx_timestamp(int fd, char* ifname);

int get_tx_timestamp(int fd);

#endif
