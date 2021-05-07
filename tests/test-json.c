/*
 *  (C) Copyright 2021 Kontron Europe GmbH, Saarbruecken
 */
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <stdint.h>
#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "../json.c"


/*
 * TESTS
 */
static void test_json_error(void)
{
	struct result result;
	json_t *j;
    char *s;

	result.dropped = 0;
	result.seq_error = FALSE;
	j = json_error(&result);
    g_assert(j != NULL);
    s = json_dumps(j, JSON_COMPACT);
    g_assert_cmpstr(s, ==, "{\"type\":\"rx-error\",\"object\":{\"dropped-packets\":0,\"sequence-error\":false}}");
	free(s);

	result.dropped = 100;
	result.seq_error = TRUE;
	j = json_error(&result);
    g_assert(j != NULL);
    s = json_dumps(j, JSON_COMPACT);
    g_assert_cmpstr(s, ==, "{\"type\":\"rx-error\",\"object\":{\"dropped-packets\":100,\"sequence-error\":true}}");
	free(s);
}

static void test_json_test_packet(void)
{
	json_t *j;
    char *s;
	struct ether_testpacket tp1;
	struct ether_testpacket tp2;
	struct timespec  tss[MAX_TS_RX];

	memset(&tp1, 0, sizeof(tp1));
	memset(&tp2, 0, sizeof(tp2));
	memset(&tss, 0, sizeof(tss));

	j = json_test_packet(&tp1, &tp2, tss);
    g_assert(j != NULL);
    s = json_dumps(j, JSON_COMPACT);
    g_assert_cmpstr(s, ==, "{\"type\":\"rx-packet\",\"object\":{\"stream-id\":0,\"sequence-number\":0,\"interval-usec\":0,\"offset-usec\":0,\"timestamps\":{\"names\":[\"interval-start\",\"tx-wakeup\",\"tx-program\",\"tx-kernel-netsched\",\"tx-kernel-hardware\",\"rx-hardware\",\"rx-program\"],\"values\":[\"1970-01-01T00:00:00.000000000\",\"1970-01-01T00:00:00.000000000\",\"1970-01-01T00:00:00.000000000\",\"1970-01-01T00:00:00.000000000\",\"1970-01-01T00:00:00.000000000\",\"1970-01-01T00:00:00.000000000\",\"1970-01-01T00:00:00.000000000\"]}}}");
	free(s);
}

int main(int argc, char** argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/timer/test_json_error",
			test_json_error);

	g_test_add_func("/timer/test_json_test_packet",
			test_json_test_packet);

	return g_test_run();
}

