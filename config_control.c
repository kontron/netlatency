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

#include <glib.h>

#include "domain_socket.h"

#include "config_control.h"

#define CMD_MSG_SOCKET "/tmp/tx_cmd_msg.socket"

#define UNUSED(x) (void)x

/*---------------------------------------------------------------------------
 * Handle configuration-control commands
 *-------------------------------------------------------------------------*/

static void set_config_control (gchar* pCmd)
{
    gchar** pCmdEntry;
    int     iValue;

    pCmdEntry = g_strsplit_set(pCmd, "=", 2);
    if (strcmp (pCmdEntry[0], "size") == 0) {
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

static void *listen_config_control (void* arg)
{
    int client_socket[MAX_CLIENTS];
    int max_fd = 0;
    int fd_socket = -1;

    UNUSED(arg);

    memset(client_socket, 0, sizeof(client_socket));

    fd_socket = open_server_socket(CMD_MSG_SOCKET);

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
