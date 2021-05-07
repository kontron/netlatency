#ifndef __JSON_H__
#define __JSON_H__

int add_json_timestamp(json_t *object, char *name, struct timespec ts);

json_t *json_test_packet(struct ether_testpacket *tp1,
        struct ether_testpacket *tp2, struct timespec *tss);

json_t *json_error(struct result *result);

void dump_json_stdout(struct json_t *j);

#endif /* __JSON_H__ */
