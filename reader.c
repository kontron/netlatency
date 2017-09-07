#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>

#include "domain_socket.h"

int main(int argc, char *argv[])
{
	int rc;
	int fd;
	char buf[1024];

	(void)argc;
	(void)argv;

#if 0
	fd = open_server_socket(socket_path);

	while (1) {
		int client;
		if ( (client = accept(fd, NULL, NULL)) == -1) {
			perror("accept error");
			continue;
		}

		while ( (rc = read(client, buf, sizeof(buf))) > 0) {
			printf("%s", buf);
		}

		if (rc == -1) {
			perror("read");
			exit(-1);
		} else if (rc == 0) {
			printf("EOF\n");
			close(client);
		}
	}
#endif

	fd = open_client_socket(socket_path);

	while (1) {
		while ( (rc = read(fd, buf, sizeof(buf))) > 0) {
			printf("%s", buf);
		}
	}

	return 0;
}
