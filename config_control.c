/****************************************************************************
 * Configuration-control for transmit packets
 *
 * Format of commands:
 *    size=<size>          configure packet size
 ***************************************************************************/

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <glib.h>

#include "domain_socket.h"

#include "config_control.h"

#define DEF_SOCKET_PORT  6666

#define UNUSED(x) (void)x

extern gboolean o_pause_loop;
extern gint o_interval_us;

/*---------------------------------------------------------------------------
 * Handle configuration-control commands
 *-------------------------------------------------------------------------*/

static void set_config_control (gchar* pCmd)
{
    gchar** pCmdEntry;
    int     iValue;

    pCmdEntry = g_strsplit_set(pCmd, "=", 2);
    if (strcmp (pCmdEntry[0], "interval_usec") == 0) {
	guint64 interval;
        interval = g_ascii_strtoull(pCmdEntry[1], NULL, 0);
	o_interval_us = interval;

    } else if (strcmp (pCmdEntry[0], "state") == 0) {
	guint64 enable;
        enable = g_ascii_strtoull(pCmdEntry[1], NULL, 0);
	o_pause_loop = enable ? FALSE : TRUE;

    } else if (strcmp (pCmdEntry[0], "size") == 0) {
        /* set packet size */
        iValue = atoi(pCmdEntry[1]);
        if (iValue < 64) {
            fprintf(stderr, "Size must be greater than 64 (%d)\n", iValue);
        }
        else {
            o_packet_size = iValue;
        }
    }
    else {
        fprintf(stderr, "Received unknown cmd '%s'\n", pCmdEntry[0]);
    }
    g_strfreev(pCmdEntry);
}

/*---------------------------------------------------------------------------
 * Listen for configuration-control commands
 *-------------------------------------------------------------------------*/

#define MAX_CLIENTS 4

int open_server_tcp_socket(int port)
{
    struct sockaddr_in addr;
    int fd;
    int opt;

    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket error");
        exit(-1);
    }

    /* set master socket to allow multiple connections ,
     * this is just a good habit, it will work without this
     */
    opt = 1;
    if( setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0 ) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);


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

static void *listen_config_control (void* arg)
{
    int client_socket[MAX_CLIENTS];
    int max_fd = 0;
    int fd_socket = -1;

    UNUSED(arg);

    memset(client_socket, 0, sizeof(client_socket));

    fd_socket = open_server_tcp_socket(DEF_SOCKET_PORT);

    while (1) {
        int i;
        int sd;
        int activity;
        fd_set readfds;

        FD_ZERO(&readfds);
        FD_SET(fd_socket, &readfds);
        max_fd = fd_socket;

        /* add child sockets to set */
        for (i = 0 ; i < MAX_CLIENTS; i++) {
            sd = client_socket[i];
            if (sd > 0) {
                FD_SET(sd, &readfds);
            }

            if (sd > max_fd) {
                max_fd = sd;
            }
        }

        //struct timeval waitd = {0, 0};
        activity = select(max_fd + 1 , &readfds , NULL , NULL , NULL);
        if ((activity < 0) && (errno != EINTR)) {
            perror("select error");
        }

        /* check for new incoming connection */
        if (FD_ISSET(fd_socket, &readfds)) {
            int new_socket;

            if ((new_socket = accept(fd_socket, NULL, NULL)) < 0) {
                perror("accept");
                return 0;
            }

            /* todo check for max reached ... */
            for (i = 0 ; i < MAX_CLIENTS; i++) {
                if (client_socket[i] == 0) {
                    client_socket[i] = new_socket;
                    break;
                }
            }
        }

        /* handle all active connecitons */
        for (i = 0; i < MAX_CLIENTS; i++) {
            sd = client_socket[i];
            if (sd == 0) {
                continue;
            }
            if (FD_ISSET( sd , &readfds)) {
                char msg_str[256];

                memset(msg_str, 0, sizeof(msg_str));
                if (read(sd ,msg_str, sizeof(msg_str)) == 0) {
                    close(sd);
                    client_socket[i] = 0;
                    continue;
                }
                set_config_control (msg_str);
            }
        }
    }
}

/*---------------------------------------------------------------------------
 * Start configuration-control task
 *-------------------------------------------------------------------------*/

void start_config_control (void)
{
    static pthread_t tid = 0;
    int rc;

    if (tid > 0) {
        fprintf(stderr, "Configuration-Control thread already started\n");
        return;
    }

    rc = pthread_create(&tid, NULL, listen_config_control, NULL);
    if (rc) {
        perror("Create configuration-control thread");
        return;
    }
}
