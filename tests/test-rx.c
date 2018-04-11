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
    gint32 dropped_packets;
    gboolean sequence_error;

    /* initialize with sequence = 0 */
    rv = check_sequence_num(0, 0, &dropped_packets, &sequence_error, FALSE);
    g_assert_cmpint(rv, == , 0);
    g_assert_cmpint(dropped_packets, ==, 0);
    g_assert_false(sequence_error);

    rv = check_sequence_num(0, 1, &dropped_packets, &sequence_error, FALSE);
    g_assert_cmpint(rv, == , 0);
    g_assert_cmpint(dropped_packets, ==, 0);
    g_assert_false(sequence_error);


    /* initialize with sequence = 0 */
    rv = check_sequence_num(0, 8, &dropped_packets, &sequence_error, TRUE);
    g_assert_cmpint(rv, == , 0);
    rv = check_sequence_num(0, 10, &dropped_packets, &sequence_error, FALSE);
    g_assert_cmpint(rv, == , 1);
    g_assert_cmpint(dropped_packets, ==, 1);
    g_assert_false(sequence_error);


    /* initialize with sequence = 0 */
    rv = check_sequence_num(0, 100, &dropped_packets, &sequence_error, TRUE);
    g_assert_cmpint(rv, == , 0);
    rv = check_sequence_num(0, 200, &dropped_packets, &sequence_error, FALSE);
    g_assert_cmpint(rv, == , 1);
    g_assert_cmpint(dropped_packets, ==, 99);
    g_assert_false(sequence_error);


    /* initialize with sequence = 0 */
    rv = check_sequence_num(0, 200, &dropped_packets, &sequence_error, TRUE);
    g_assert_cmpint(rv, == , 0);
    rv = check_sequence_num(0, 100, &dropped_packets, &sequence_error, FALSE);
    g_assert_cmpint(rv, == , 1);
    g_assert_cmpint(dropped_packets, ==, -101);
    g_assert_true(sequence_error);
}

static void test_check_sequence_num_with_stream_id(void)
{
    int rv;
    gint32 dropped;
    gboolean seq_err;

    /* initialize with sequence = 0 */
    rv = check_sequence_num(0, 0, &dropped, &seq_err, TRUE);
    g_assert_cmpint(rv, == , 0);
    rv = check_sequence_num(1, 0, &dropped, &seq_err, TRUE);
    g_assert_cmpint(rv, == , 0);

    rv = check_sequence_num(1, 1, &dropped, &seq_err, FALSE);
    g_assert_cmpint(rv, == , 0);
    rv = check_sequence_num(1, 2, &dropped, &seq_err, FALSE);
    g_assert_cmpint(rv, == , 0);
    rv = check_sequence_num(1, 3, &dropped, &seq_err, FALSE);
    g_assert_cmpint(rv, == , 0);

    rv = check_sequence_num(0, 1, &dropped, &seq_err, FALSE);
    g_assert_cmpint(rv, == , 0);
    rv = check_sequence_num(0, 2, &dropped, &seq_err, FALSE);
    g_assert_cmpint(rv, == , 0);
    rv = check_sequence_num(0, 3, &dropped, &seq_err, FALSE);
    g_assert_cmpint(rv, == , 0);
}

static void test_check_sequence_num_stream_id_max(void)
{
    int rv;
    gint32 dropped;
    gboolean seq_err;

    rv = check_sequence_num(32, 0, &dropped, &seq_err, TRUE);
    g_assert_cmpint(rv, == , 0);

    rv = check_sequence_num(33, 0, &dropped, &seq_err, TRUE);
    g_assert_cmpint(rv, == , -1);
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

	g_test_add_func("/rx/check_sequence_num/valid",
            test_check_sequence_num);
	g_test_add_func("/rx/check_sequence_num/stream_id",
            test_check_sequence_num_with_stream_id);
	g_test_add_func("/rx/check_sequence_num/stream_id_max",
            test_check_sequence_num_stream_id_max);

	g_test_add_func("/rx/is_broadcast_addr",
            test_is_broadcast_addr);

	return g_test_run();
}

