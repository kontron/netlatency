#include <assert.h>

#include <glib.h>
#include <jansson.h>

#include "data.h"
#include "timer.h"

int add_json_timestamp(json_t *object, char *name, struct timespec ts)
{
    char *s;
    json_t *n;
    json_t *v;

    /* Add to names array */
    n = json_object_get(object, "names");
    if (n == NULL) {
        assert(0);
    }
    json_array_append_new(n, json_string(name));

    /* Add to values array */
    v = json_object_get(object, "values");
    if (v == NULL) {
        assert(0);
    }

    s = timespec_to_iso_string(&ts);
    json_array_append_new(v, json_string(s));
    g_free(s);

    return 0;
}

json_t *json_test_packet(struct ether_testpacket *tp1,
        struct ether_testpacket *tp2, struct timespec *tss)
{
    g_assert(tp1);
    g_assert(tp2);
    g_assert(tss);

    json_t *root = json_object();
    json_t *object = json_object();
    json_t *timestamps = json_object();

    json_object_set_new(root, "type", json_string("rx-packet"));
    json_object_set_new(root, "object", object);

    json_object_set_new(object, "stream-id", json_integer(tp1->stream_id));
    json_object_set_new(object, "sequence-number", json_integer(tp1->seq));
    json_object_set_new(object, "interval-usec", json_integer(tp1->interval_usec));
    json_object_set_new(object, "offset-usec", json_integer(tp1->offset_usec));

    json_object_set_new(object, "timestamps", timestamps);
    json_object_set_new(timestamps, "names", json_array());
    json_object_set_new(timestamps, "values", json_array());

    /* copy the timestamps to ts to avoid unaligned pointer compiler errors */
    add_json_timestamp(timestamps, "interval-start", tp1->timestamps[TS_T0]);
    add_json_timestamp(timestamps, "tx-wakeup", tp1->timestamps[TS_WAKEUP]);
    add_json_timestamp(timestamps, "tx-program", tp1->timestamps[TS_PROG_SEND]);
    add_json_timestamp(timestamps, "tx-kernel-netsched", tp2->timestamps[TS_LAST_KERNEL_SCHED]);
    add_json_timestamp(timestamps, "tx-kernel-hardware", tp2->timestamps[TS_LAST_KERNEL_SW_TX]);

    add_json_timestamp(timestamps, "rx-hardware", tss[TS_KERNEL_HW_RX]);
    //add_json_timestamp(timestamps, "rx-kernel-driver", &tss[TS_KERNEL_SW_RX]);
    add_json_timestamp(timestamps, "rx-program", tss[TS_PROG_RECV]);

    return root;
}

json_t *json_error(struct result *result)
{
    json_t *j;

    j = json_pack("{sss{sisb}}",
                  "type", "rx-error",
                  "object",
                  "dropped-packets", result->dropped,
                  "sequence-error", result->seq_error
    );

    return j;
}

void dump_json_stdout(struct json_t *j)
{
    char *s = json_dumps(j, JSON_COMPACT);
    if (s) {
        printf("%s\n", s);
        fflush(stdout);
        free(s);
    }
}
