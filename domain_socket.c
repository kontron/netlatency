#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <signal.h>


int open_client_socket(char *path)
{
	int fd;
	struct sockaddr_un addr;

	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		perror("socket error");
		exit(-1);
	}
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;

	strncpy(addr.sun_path, path, sizeof(addr.sun_path)-1);

	if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		perror("connect error");
		exit(-1);
	}

	return fd;
}

int open_server_socket(char *socket_path)
{
	struct sockaddr_un addr;
	int fd;

	umask(0);

	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		perror("socket error");
		exit(-1);
	}

	signal(SIGPIPE, SIG_IGN);

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	if (*socket_path == '\0') {
		*addr.sun_path = '\0';
		strncpy(addr.sun_path+1, socket_path+1, sizeof(addr.sun_path)-2);
	} else {
		strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path)-1);
		unlink(socket_path);
	}

	if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		perror("bind error");
		exit(-1);
	}

	if (listen(fd, 5) == -1) {
		perror("listen error");
		exit(-1);
	}

	return fd;
}
