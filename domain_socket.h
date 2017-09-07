#ifndef __DOMAIN_SOCKET_H__
#define __DOMAIN_SOCKET_H__

char *socket_path = "/tmp/jitter.socket";

int open_client_socket(char *path);

int open_server_socket(char *socket_path);

#endif
