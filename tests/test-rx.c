/*
 *  (C) Copyright 2014 Kontron Europe GmbH, Saarbruecken
 */
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <stdint.h>
#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>


void timespec_diff(const struct timespec *a, const struct timespec *b,
                   struct timespec *result)
{
    (void)a;
    (void)b;
    (void)result;
}

char *timespec_to_iso_string(struct timespec *time)
{
    (void)time;
    return NULL;
}

int open_server_socket(char *socket_path)
{
    (void)socket_path;
    return -1;
}



#define main old_main
#include "../rx.c"
#undef main

/*
 * TESTS
 */
static void test_check_sequence_num(void)
{
    int rv;
    long dropped_packets;
    gboolean sequence_error;

    /* initialize with sequence = 0 */
    rv = check_sequence_num(0, &dropped_packets, &sequence_error, FALSE);
    g_assert_cmpint(rv, == , 0);
    g_assert_cmpint(dropped_packets, ==, 0);
    g_assert_false(sequence_error);

    rv = check_sequence_num(1, &dropped_packets, &sequence_error, FALSE);
    g_assert_cmpint(rv, == , 0);
    g_assert_cmpint(dropped_packets, ==, 0);
    g_assert_false(sequence_error);


    /* initialize with sequence = 0 */
    rv = check_sequence_num(8, &dropped_packets, &sequence_error, TRUE);
    g_assert_cmpint(rv, == , 0);
    rv = check_sequence_num(10, &dropped_packets, &sequence_error, FALSE);
    g_assert_cmpint(rv, == , 1);
    g_assert_cmpint(dropped_packets, ==, 1);
    g_assert_false(sequence_error);


    /* initialize with sequence = 0 */
    rv = check_sequence_num(100, &dropped_packets, &sequence_error, TRUE);
    g_assert_cmpint(rv, == , 0);
    rv = check_sequence_num(200, &dropped_packets, &sequence_error, FALSE);
    g_assert_cmpint(rv, == , 1);
    g_assert_cmpint(dropped_packets, ==, 99);
    g_assert_false(sequence_error);


    /* initialize with sequence = 0 */
    rv = check_sequence_num(200, &dropped_packets, &sequence_error, TRUE);
    g_assert_cmpint(rv, == , 0);
    rv = check_sequence_num(100, &dropped_packets, &sequence_error, FALSE);
    g_assert_cmpint(rv, == , 1);
    g_assert_cmpint(dropped_packets, ==, -101);
    g_assert_true(sequence_error);
}

static void test_is_broadcast_addr(void)
{
    guint8 addr[ETH_ALEN];

    memcpy(&addr, "\xff\xff\xff\xff\xff\xff", ETH_ALEN);
    g_assert_true(is_broadcast_addr(addr));

    memcpy(&addr, "\xff\xff\xff\xff\xff\xfe", ETH_ALEN);
    g_assert_false(is_broadcast_addr(addr));
}


int main(int argc, char** argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/rx/check_sequence_num",
            test_check_sequence_num);
	g_test_add_func("/rx/is_broadcast_addr",
            test_is_broadcast_addr);

	return g_test_run();
}

