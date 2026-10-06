#ifndef PUBSUB_H
#define PUBSUB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Set these consistently for every translation unit, e.g. with -D. */
#ifndef PS_MAX_TOPICS
#define PS_MAX_TOPICS 32
#endif
#ifndef PS_MAX_CLIENTS
#define PS_MAX_CLIENTS 8
#endif
#ifndef PS_TOPIC_NAME_SIZE
#define PS_TOPIC_NAME_SIZE 32
#endif
#ifndef PS_MAX_PAYLOAD
#define PS_MAX_PAYLOAD 64
#endif

#if PS_MAX_TOPICS < 1 || PS_MAX_TOPICS > 32
#error PS_MAX_TOPICS_must_be_between_1_and_32
#endif
#if PS_MAX_CLIENTS < 1 || PS_TOPIC_NAME_SIZE < 3
#error Invalid_pubsub_capacity
#endif
#if PS_MAX_PAYLOAD < 1 || PS_MAX_PAYLOAD > 65535
#error Invalid_pubsub_payload_size
#endif

typedef uint16_t PsTopic;
#define PS_TOPIC_INVALID ((PsTopic)UINT16_MAX)
#define PS_WAIT_FOREVER UINT32_MAX
typedef struct PsClient PsClient;

typedef enum {
    PS_OK = 0,
    PS_INVALID_ARGUMENT,
    PS_NOT_INITIALIZED,
    PS_NO_MEMORY,
    PS_LIMIT,
    PS_NOT_FOUND,
    PS_SIZE_MISMATCH,
    PS_PARTIAL,              /* Some (possibly all) subscriber queues were full. */
    PS_TIMEOUT
} PsStatus;

typedef struct {
    PsTopic topic;
    uint16_t size;
    uint32_t sequence;       /* Per topic; starts at 1, wraps modulo 2^32. */
    uint64_t timestamp_ms;   /* Monotonic milliseconds at publication. */
    uint8_t data[PS_MAX_PAYLOAD];
} PsMessage;

typedef struct {
    uint32_t matched;
    uint32_t delivered;
    uint32_t dropped;
} PsPublishReport;

typedef struct {
    uint32_t delivered;
    uint32_t dropped;
    uint32_t received;
} PsClientStats;

/* Initialize in single-task startup before any concurrent pubsub calls.
 * May be called before the scheduler or from ESP-IDF app_main().
 * Repeated calls do not reset.
 * Other APIs are task-context APIs (never ISR APIs). Registration and client
 * creation may also be used during single-threaded startup.
 * Clients/topics live for the lifetime of the application; no delete/reset API.
 */
PsStatus ps_init(void);
PsStatus ps_advertise(const char *name, size_t payload_size, PsTopic *topic);
PsStatus ps_topic_find(const char *name, PsTopic *topic);
const char *ps_topic_name(PsTopic topic); /* Stable string, NULL if invalid. */
PsStatus ps_client_create(uint32_t queue_depth, PsClient **client);

/* Exact child-topic names, e.g. "/b/1". Idempotent; no wildcards or replay.
 * Unsubscribe prevents future enqueueing; already queued messages remain.
 */
PsStatus ps_subscribe(PsClient *client, const char *name);
PsStatus ps_unsubscribe(PsClient *client, const char *name);

/* Copy to each subscribed client's queue, with no wait for queue space.
 * Drop newest on a full queue. Report is optional, zeroed even on failure.
 * PS_OK + matched == 0 means there were no subscribers.
 * Internal mutex acquisition may block; this is not a lock-free API.
 */
PsStatus ps_publish(PsTopic topic, const void *payload, size_t size,
                    PsPublishReport *report);

/* One receiving task per client. Timeout is in milliseconds:
 * 0 = poll, PS_WAIT_FOREVER = wait indefinitely.
 * Mutex and event waits share one timeout.
 * On success, memcpy data into your payload struct (do not cast data).
 */
PsStatus ps_receive(PsClient *client, PsMessage *message, uint32_t timeout_ms);
PsStatus ps_client_stats(PsClient *client, PsClientStats *stats);

#ifdef __cplusplus
}
#endif
#endif
