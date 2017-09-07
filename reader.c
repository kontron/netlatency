#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>

#include "domain_socket.h"

int main(int argc, char *argv[])
{
//	int rc;
	int fd;
	char buf[1024];

	(void)argc;
	(void)argv;

	fd = open_client_socket(socket_path);

	while (1) {
		memset(buf, 0, sizeof(buf));
		read(fd, buf, sizeof(buf));
		printf("%s", buf);
	}

	return 0;
}
