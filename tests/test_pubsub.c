#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "pubsub.h"
#include "host_rtos.h"
#include "pubsub_port.h"
#include "FreeRTOS.h" /* Adapter tests only: application API does not need it. */

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
    ExitProcess(1); \
} } while (0)

static PsTopic topic[4][3];
static PsClient *a, *b;
static const char *const names[4][3] = {
    {"/a/1", "/a/2", "/a/3"}, {"/b/1", "/b/2", "/b/3"},
    {"/c/1", "/c/2", "/c/3"}, {"/d/1", "/d/2", "/d/3"}
};

static uint32_t value_of(const PsMessage *message)
{
    uint32_t value;
    CHECK(message->size == sizeof(value));
    memcpy(&value, message->data, sizeof(value));
    return value;
}

static void setup_and_validation(void)
{
    PsTopic found;
    PsClient *client;
    PsMessage message;
    unsigned int before, node, child;
    CHECK(ps_topic_find("/a/1", &found) == PS_NOT_INITIALIZED);
    CHECK(ps_receive(NULL, &message, 0) == PS_INVALID_ARGUMENT);
    host_fail_after(0);
    CHECK(ps_init() == PS_NO_MEMORY);
    host_fail_after(-1);
    CHECK(ps_init() == PS_OK);
    CHECK(ps_init() == PS_OK);
    for (node = 0; node < 4; ++node) {
        for (child = 0; child < 3; ++child) {
            CHECK(ps_advertise(names[node][child], sizeof(uint32_t),
                               &topic[node][child]) == PS_OK);
        }
    }
    CHECK(ps_advertise("/a/1", sizeof(uint32_t), &found) == PS_OK);
    CHECK(found == topic[0][0]);
    CHECK(ps_advertise("/a/1", 1, &found) == PS_SIZE_MISMATCH);
    CHECK(found == PS_TOPIC_INVALID);
    CHECK(ps_advertise("/", 4, &found) == PS_INVALID_ARGUMENT);
    CHECK(ps_advertise("/a//1", 4, &found) == PS_INVALID_ARGUMENT);
    CHECK(ps_advertise("/a/1/", 4, &found) == PS_INVALID_ARGUMENT);
    CHECK(ps_advertise("/a/*", 4, &found) == PS_INVALID_ARGUMENT);
    CHECK(ps_advertise("/too-big", PS_MAX_PAYLOAD + 1, &found) == PS_INVALID_ARGUMENT);
    CHECK(ps_topic_find("/missing/1", &found) == PS_NOT_FOUND);
    CHECK(ps_topic_name(PS_TOPIC_INVALID) == NULL);
    CHECK(strcmp(ps_topic_name(topic[2][1]), "/c/2") == 0);
    CHECK(ps_client_create(0, &client) == PS_INVALID_ARGUMENT);
    before = host_live_allocations();
    host_fail_after(0);
    CHECK(ps_client_create(2, &client) == PS_NO_MEMORY && client == NULL);
    host_fail_after(1); /* Queue succeeds; event allocation fails. */
    CHECK(ps_client_create(2, &client) == PS_NO_MEMORY && client == NULL);
    CHECK(host_live_allocations() == before);
    host_fail_after(-1);
    CHECK(ps_client_create(2, &a) == PS_OK);
    CHECK(ps_client_create(4, &b) == PS_OK);
    CHECK(ps_subscribe(NULL, "/a/1") == PS_INVALID_ARGUMENT);
    CHECK(ps_subscribe(a, "/b/9") == PS_NOT_FOUND);
    CHECK(ps_subscribe(a, "/b/1") == PS_OK);
    CHECK(ps_subscribe(a, "/b/1") == PS_OK); /* No duplicate deliveries. */
    CHECK(ps_subscribe(a, "/c/2") == PS_OK);
    CHECK(ps_subscribe(a, "/d/3") == PS_OK);
    CHECK(ps_subscribe(b, "/c/1") == PS_OK);
    CHECK(ps_subscribe(b, "/c/2") == PS_OK);
    puts("PASS initialization, allocation failure, child topic validation");
}

static void routing_copy_and_overflow(void)
{
    uint32_t value = 11;
    PsMessage ma, mb;
    PsPublishReport report;
    PsClientStats stats;
    CHECK(ps_publish(topic[1][1], &value, sizeof(value), &report) == PS_OK);
    CHECK(report.matched == 0);
    CHECK(ps_receive(a, &ma, 0) == PS_TIMEOUT);
    CHECK(ps_publish(topic[2][1], &value, 1, &report) == PS_SIZE_MISMATCH);
    CHECK(report.matched == 0);
    CHECK(ps_publish(PS_TOPIC_INVALID, &value, sizeof(value), NULL) == PS_NOT_FOUND);
    CHECK(ps_publish(topic[2][1], &value, sizeof(value), &report) == PS_OK);
    CHECK(report.matched == 2 && report.delivered == 2 && report.dropped == 0);
    value = 99; /* Publishing copied the old value. */
    CHECK(ps_receive(a, &ma, 0) == PS_OK);
    CHECK(ps_receive(b, &mb, 0) == PS_OK);
    CHECK(ma.topic == topic[2][1] && ma.sequence == mb.sequence);
    CHECK(value_of(&ma) == 11 && value_of(&mb) == 11);
    ma.data[0] = 0;
    CHECK(value_of(&mb) == 11); /* Subscribers own separate copies. */
    CHECK(ps_receive(a, &ma, 0) == PS_TIMEOUT);

    value = 1;
    CHECK(ps_publish(topic[1][0], &value, sizeof(value), NULL) == PS_OK);
    value = 2;
    CHECK(ps_publish(topic[3][2], &value, sizeof(value), NULL) == PS_OK);
    value = 3;
    CHECK(ps_publish(topic[2][1], &value, sizeof(value), &report) == PS_PARTIAL);
    CHECK(report.matched == 2 && report.delivered == 1 && report.dropped == 1);
    CHECK(ps_receive(a, &ma, 0) == PS_OK && value_of(&ma) == 1);
    CHECK(ps_receive(a, &ma, 0) == PS_OK && value_of(&ma) == 2);
    CHECK(ps_receive(a, &ma, 0) == PS_TIMEOUT);
    CHECK(ps_receive(b, &mb, 0) == PS_OK && value_of(&mb) == 3);
    CHECK(ps_client_stats(a, &stats) == PS_OK);
    CHECK(stats.delivered == 3 && stats.received == 3 && stats.dropped == 1);

    value = 4;
    CHECK(ps_publish(topic[1][0], &value, sizeof(value), NULL) == PS_OK);
    CHECK(ps_unsubscribe(a, "/b/1") == PS_OK);
    CHECK(ps_unsubscribe(a, "/b/1") == PS_OK);
    CHECK(ps_receive(a, &ma, 0) == PS_OK && value_of(&ma) == 4);
    CHECK(ps_publish(topic[1][0], &value, sizeof(value), &report) == PS_OK);
    CHECK(report.matched == 0);
    puts("PASS exact routing, fanout, ownership, FIFO, overflow, unsubscribe");
}

static void publish_in_wait_gap(void)
{
    uint32_t value = 77;
    CHECK(ps_publish(topic[3][2], &value, sizeof(value), NULL) == PS_OK);
}

static DWORD WINAPI delayed_publish(void *unused)
{
    (void)unused;
    Sleep(30);
    publish_in_wait_gap();
    return 0;
}

static void wait_and_wakeup(void)
{
    PsMessage message;
    HANDLE thread;
    ULONGLONG start;
    host_before_event_wait(publish_in_wait_gap);
    CHECK(ps_receive(a, &message, 200) == PS_OK);
    CHECK(value_of(&message) == 77);
    thread = CreateThread(NULL, 0, delayed_publish, NULL, 0, NULL);
    CHECK(thread != NULL);
    CHECK(ps_receive(a, &message, PS_WAIT_FOREVER) == PS_OK);
    CHECK(value_of(&message) == 77);
    CHECK(WaitForSingleObject(thread, 2000) == WAIT_OBJECT_0);
    CloseHandle(thread);
    start = GetTickCount64();
    CHECK(ps_receive(a, &message, 30) == PS_TIMEOUT);
    CHECK(GetTickCount64() - start >= 20);
    CHECK(GetTickCount64() - start < 1000);
    puts("PASS publish-before-wait race, blocked wakeup, finite timeout");
}

typedef struct {
    PsClient *client;
    PsTopic topic;
    unsigned int count;
} ConcurrentArgs;

static DWORD WINAPI producer(void *argument)
{
    ConcurrentArgs *args = argument;
    uint32_t value;
    for (value = 0; value < args->count; ++value) {
        PsPublishReport report;
        CHECK(ps_publish(args->topic, &value, sizeof(value), &report) == PS_OK);
        CHECK(report.matched == 2 && report.delivered == 2);
    }
    return 0;
}

static DWORD WINAPI consumer(void *argument)
{
    ConcurrentArgs *args = argument;
    uint32_t next[2] = {0};
    uint32_t sequences[2] = {0};
    unsigned int i;
    for (i = 0; i < args->count * 2; ++i) {
        PsMessage message;
        unsigned int index;
        CHECK(ps_receive(args->client, &message, 2000) == PS_OK);
        CHECK(message.topic == topic[0][0] || message.topic == topic[0][1]);
        index = message.topic == topic[0][0] ? 0U : 1U;
        CHECK(value_of(&message) == next[index]++);
        CHECK(message.sequence == ++sequences[index]);
    }
    CHECK(next[0] == args->count && next[1] == args->count);
    return 0;
}

static void concurrent_fanout(void)
{
    enum { COUNT = 1000 };
    PsClient *c, *d;
    HANDLE threads[4];
    ConcurrentArgs args[4];
    PsClientStats stats;
    unsigned int i;
    CHECK(ps_client_create(COUNT * 2, &c) == PS_OK);
    CHECK(ps_client_create(COUNT * 2, &d) == PS_OK);
    CHECK(ps_subscribe(c, "/a/1") == PS_OK && ps_subscribe(c, "/a/2") == PS_OK);
    CHECK(ps_subscribe(d, "/a/1") == PS_OK && ps_subscribe(d, "/a/2") == PS_OK);
    args[0] = (ConcurrentArgs){c, 0, COUNT};
    args[1] = (ConcurrentArgs){d, 0, COUNT};
    args[2] = (ConcurrentArgs){NULL, topic[0][0], COUNT};
    args[3] = (ConcurrentArgs){NULL, topic[0][1], COUNT};
    for (i = 0; i < 4; ++i) {
        threads[i] = CreateThread(NULL, 0, i < 2 ? consumer : producer, &args[i], 0, NULL);
        CHECK(threads[i] != NULL);
    }
    CHECK(WaitForMultipleObjects(4, threads, TRUE, 10000) == WAIT_OBJECT_0);
    for (i = 0; i < 4; ++i) {
        CloseHandle(threads[i]);
    }
    CHECK(ps_client_stats(c, &stats) == PS_OK);
    CHECK(stats.delivered == COUNT * 2 && stats.received == COUNT * 2 && stats.dropped == 0);
    CHECK(ps_client_stats(d, &stats) == PS_OK);
    CHECK(stats.delivered == COUNT * 2 && stats.received == COUNT * 2 && stats.dropped == 0);
    puts("PASS concurrent producers + consumers: 2000 publishes, 4000 deliveries");
}

static void capacity_and_high_bit(void)
{
    PsTopic last = PS_TOPIC_INVALID;
    PsClient *client = NULL;
    unsigned int i;
    char name[32];
    uint32_t value = 123;
    PsMessage message;
    for (i = 12; i < PS_MAX_TOPICS; ++i) {
        (void)snprintf(name, sizeof(name), "/extra/%u", i);
        CHECK(ps_advertise(name, sizeof(value), &last) == PS_OK);
    }
    CHECK(last == PS_MAX_TOPICS - 1);
    CHECK(ps_advertise("/overflow/topic", 4, &last) == PS_LIMIT);
    for (i = 4; i < PS_MAX_CLIENTS; ++i) {
        CHECK(ps_client_create(1, &client) == PS_OK);
    }
    CHECK(ps_subscribe(client, name) == PS_OK);
    CHECK(ps_publish(PS_MAX_TOPICS - 1, &value, sizeof(value), NULL) == PS_OK);
    CHECK(ps_receive(client, &message, 0) == PS_OK && value_of(&message) == value);
    CHECK(ps_client_create(1, &client) == PS_LIMIT);
    puts("PASS client/topic limits and topic 31 subscription mask");
}

static PsPortEvent port_test_event;

static void set_port_event(void)
{
    ps_port_event_set(port_test_event);
}

static void port_clock_and_timeouts(void)
{
    uint64_t before = ps_port_now_ms();
    uint64_t advanced_ticks = (uint64_t)UINT32_MAX + 11U;
    uint64_t expected_ms = advanced_ticks * 1000U / configTICK_RATE_HZ;
    uint64_t after;
    /* Multiple native tick wraps must not reset the public clock. */
    host_advance_ticks(advanced_ticks);
    after = ps_port_now_ms();
    CHECK(after >= before && after - before + 1U >= expected_ms);
    CHECK(after - before < expected_ms + 1000U);

    port_test_event = ps_port_event_create();
    CHECK(port_test_event != NULL);
    host_before_event_wait(set_port_event);
    CHECK(ps_port_event_wait(port_test_event, 1));
    CHECK(host_last_event_wait_ticks() == (configTICK_RATE_HZ + 999U) / 1000U);
    ps_port_event_clear(port_test_event);
    host_before_event_wait(set_port_event);
    CHECK(ps_port_event_wait(port_test_event, PS_PORT_WAIT_FOREVER - 1U));
    {
        uint64_t ticks = ((uint64_t)(PS_PORT_WAIT_FOREVER - 1U) *
                          configTICK_RATE_HZ + 999U) / 1000U;
        if (ticks >= portMAX_DELAY) ticks = (uint64_t)portMAX_DELAY - 1U;
        CHECK(host_last_event_wait_ticks() == ticks);
    }
    CHECK(host_last_event_wait_ticks() != portMAX_DELAY);
    ps_port_event_clear(port_test_event);
    host_before_event_wait(set_port_event);
    CHECK(ps_port_event_wait(port_test_event, PS_PORT_WAIT_FOREVER));
    CHECK(host_last_event_wait_ticks() == portMAX_DELAY);
    ps_port_event_clear(port_test_event);
    puts("PASS port: monotonic tick-wrap clock, ms rounding, finite/infinite wait mapping");
}

int main(void)
{
    setup_and_validation();
    port_clock_and_timeouts();
    routing_copy_and_overflow();
    wait_and_wakeup();
    concurrent_fanout();
    capacity_and_high_bit();
    puts("All pubsub host tests passed.");
    return 0;
}
