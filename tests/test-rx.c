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



#define main old_main
#include "../rx.c"
#undef main

/*
 * TESTS
 */
static void test_check_sequence_num(void)
{
    struct result r;

    r.tp = g_new0(struct ether_testpacket, 1);
    r.last_tp = g_new0(struct ether_testpacket, 1);

    r.last_tp->seq = 1;
    r.tp->seq = 2;
    check_sequence_num(&r);
    g_assert_false(r.seq_error);
    g_assert_cmpint(r.dropped, ==, 0);

    r.last_tp->seq = 1;
    r.tp->seq = 3;
    check_sequence_num(&r);
    g_assert_false(r.seq_error);
    g_assert_cmpint(r.dropped, ==, 1);

    r.last_tp->seq = 3;
    r.tp->seq = 2;
    check_sequence_num(&r);
    g_assert_true(r.seq_error);
    g_assert_cmpint(r.dropped, ==, 0);

    g_free(r.tp);
    g_free(r.last_tp);
}

#if 0
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

    rv = check_sequence_num(15, 0, &dropped, &seq_err, TRUE);
    g_assert_cmpint(rv, == , 0);

    rv = check_sequence_num(16, 0, &dropped, &seq_err, TRUE);
    g_assert_cmpint(rv, == , -1);
}
#endif

static void test_is_broadcast_addr(void)
{
    guint8 addr[ETH_ALEN];

    memcpy(&addr, "\xff\xff\xff\xff\xff\xff", ETH_ALEN);
    g_assert_true(is_broadcast_addr(addr));

    memcpy(&addr, "\xff\xff\xff\xff\xff\xfe", ETH_ALEN);
    g_assert_false(is_broadcast_addr(addr));
}

static void test_dump_json_test_packet(void)
{
    struct ether_testpacket _tp, *tp = &_tp;
    struct timespec tss[3];
    json_t *j = NULL;

    memset(tp, 0, sizeof(*tp));
    memset(tss, 0, sizeof(tss));
    j = json_test_packet(tp, tp, tss);
    g_assert(j != NULL);
#if 0
    // test cannot be done here because order of elements depends on machine!
    // it is a dict

    g_assert_cmpstr(str, ==,
    "{\"type\":\"rx-packet\",\"object\":{\"stream-id\":0,\"sequence-number\":0,\"interval-usec\":0,\"offset-usec\":0,\"timestamps\":{\"names\":[\"interval-start\",\"tx-wakeup\",\"tx-program\",\"tx-kernel-netsched\",\"tx-kernel-driver\",\"rx-hardware\",\"rx-kernerl-driver\",\"rx-program\"],\"values\":[\"1970-01-01T00:00:00.000000000Z\",\"1970-01-01T00:00:00.000000000Z\",\"1970-01-01T00:00:00.000000000Z\",\"1970-01-01T00:00:00.000000000Z\",\"1970-01-01T00:00:00.000000000Z\",\"1970-01-01T00:00:00.000000000Z\",\"1970-01-01T00:00:00.000000000Z\",\"1970-01-01T00:00:00.000000000Z\"]}}}");
#endif
    json_decref(j);
}


int main(int argc, char** argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/rx/check_sequence_num/valid",
         test_check_sequence_num);
#if 0
    g_test_add_func("/rx/check_sequence_num/stream_id",
           test_check_sequence_num_with_stream_id);
    g_test_add_func("/rx/check_sequence_num/stream_id_max",
           test_check_sequence_num_stream_id_max);
#endif

    g_test_add_func("/rx/is_broadcast_addr",
           test_is_broadcast_addr);

    g_test_add_func("/rx/dump_json_test_packet",
            test_dump_json_test_packet);

    return g_test_run();
}

