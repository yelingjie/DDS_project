#include "pubsub.h"

#include <string.h>
#include "pubsub_port.h"

typedef struct {
    char name[PS_TOPIC_NAME_SIZE];
    uint16_t size;
    uint32_t sequence;
} TopicEntry;

struct PsClient {
    PsPortQueue queue;
    PsPortEvent events;
    uint32_t subscriptions;
    PsClientStats stats;
};

static PsPortMutex bus_lock;
static TopicEntry topics[PS_MAX_TOPICS];
static PsClient clients[PS_MAX_CLIENTS];
static size_t topic_count;
static size_t client_count;

static PsStatus lock_bus(uint32_t timeout)
{
    if (bus_lock == NULL) {
        return PS_NOT_INITIALIZED;
    }
    return ps_port_mutex_lock(bus_lock, timeout) ? PS_OK : PS_TIMEOUT;
}

static void unlock_bus(void)
{
    ps_port_mutex_unlock(bus_lock);
}

/* Only normalized absolute paths. '/b/1' and '/b/2' are distinct topics. */
static bool valid_name(const char *name)
{
    size_t i;
    if (name == NULL || name[0] != '/') {
        return false;
    }
    for (i = 1; i < PS_TOPIC_NAME_SIZE; ++i) {
        if (name[i] == '\0') {
            return name[i - 1] != '/' ? true : false;
        }
        if ((name[i] == '/' && name[i - 1] == '/') ||
            name[i] == '*' || name[i] == '?') {
            return false;
        }
    }
    return false;
}

/* Call with bus_lock held. Never dereference an unvalidated client pointer. */
static bool valid_client(const PsClient *client)
{
    size_t i;
    for (i = 0; i < client_count; ++i) {
        if (client == &clients[i]) {
            return true;
        }
    }
    return false;
}

static PsTopic find_topic(const char *name)
{
    size_t i;
    for (i = 0; i < topic_count; ++i) {
        if (strcmp(topics[i].name, name) == 0) {
            return (PsTopic)i;
        }
    }
    return PS_TOPIC_INVALID;
}

PsStatus ps_init(void)
{
    /* Startup only: clients, topics, and their handles are never destroyed. */
    if (bus_lock == NULL) {
        bus_lock = ps_port_mutex_create();
    }
    return bus_lock != NULL ? PS_OK : PS_NO_MEMORY;
}

PsStatus ps_advertise(const char *name, size_t payload_size, PsTopic *topic)
{
    PsTopic found;
    PsStatus status;
    if (topic != NULL) {
        *topic = PS_TOPIC_INVALID;
    }
    if (topic == NULL || valid_name(name) != true ||
        payload_size == 0 || payload_size > PS_MAX_PAYLOAD) {
        return PS_INVALID_ARGUMENT;
    }
    status = lock_bus(PS_WAIT_FOREVER);
    if (status != PS_OK) {
        return status;
    }
    found = find_topic(name);
    if (found != PS_TOPIC_INVALID) {
        status = topics[found].size == payload_size ? PS_OK : PS_SIZE_MISMATCH;
        if (status == PS_OK) {
            *topic = found;
        }
    } else if (topic_count == PS_MAX_TOPICS) {
        status = PS_LIMIT;
    } else {
        TopicEntry *entry = &topics[topic_count];
        memcpy(entry->name, name, strlen(name) + 1);
        entry->size = (uint16_t)payload_size;
        *topic = (PsTopic)topic_count++;
    }
    unlock_bus();
    return status;
}

PsStatus ps_topic_find(const char *name, PsTopic *topic)
{
    PsStatus status;
    if (topic != NULL) {
        *topic = PS_TOPIC_INVALID;
    }
    if (topic == NULL || valid_name(name) != true) {
        return PS_INVALID_ARGUMENT;
    }
    status = lock_bus(PS_WAIT_FOREVER);
    if (status != PS_OK) {
        return status;
    }
    *topic = find_topic(name);
    status = *topic == PS_TOPIC_INVALID ? PS_NOT_FOUND : PS_OK;
    unlock_bus();
    return status;
}

const char *ps_topic_name(PsTopic topic)
{
    const char *name = NULL;
    if (lock_bus(PS_WAIT_FOREVER) == PS_OK) {
        if (topic < topic_count) {
            name = topics[topic].name;
        }
        unlock_bus();
    }
    return name;
}

PsStatus ps_client_create(uint32_t queue_depth, PsClient **client)
{
    PsStatus status;
    PsClient *entry;
    if (client != NULL) {
        *client = NULL;
    }
    if (client == NULL || queue_depth == 0) {
        return PS_INVALID_ARGUMENT;
    }
    status = lock_bus(PS_WAIT_FOREVER);
    if (status != PS_OK) {
        return status;
    }
    if (client_count == PS_MAX_CLIENTS) {
        unlock_bus();
        return PS_LIMIT;
    }
    entry = &clients[client_count];
    entry->queue = ps_port_queue_create(queue_depth, sizeof(PsMessage));
    if (entry->queue == NULL) {
        unlock_bus();
        return PS_NO_MEMORY;
    }
    entry->events = ps_port_event_create();
    if (entry->events == NULL) {
        ps_port_queue_destroy(entry->queue);
        entry->queue = NULL;
        unlock_bus();
        return PS_NO_MEMORY;
    }
    *client = entry;
    ++client_count;
    unlock_bus();
    return PS_OK;
}

static PsStatus update_subscription(PsClient *client, const char *name,
                                    bool subscribe)
{
    PsTopic topic;
    PsStatus status;
    if (valid_name(name) != true) {
        return PS_INVALID_ARGUMENT;
    }
    status = lock_bus(PS_WAIT_FOREVER);
    if (status != PS_OK) {
        return status;
    }
    if (valid_client(client) != true) {
        status = PS_INVALID_ARGUMENT;
    } else if ((topic = find_topic(name)) == PS_TOPIC_INVALID) {
        status = PS_NOT_FOUND;
    } else if (subscribe == true) {
        client->subscriptions |= UINT32_C(1) << topic;
    } else {
        client->subscriptions &= ~(UINT32_C(1) << topic);
    }
    unlock_bus();
    return status;
}

PsStatus ps_subscribe(PsClient *client, const char *name)
{
    return update_subscription(client, name, true);
}

PsStatus ps_unsubscribe(PsClient *client, const char *name)
{
    return update_subscription(client, name, false);
}

PsStatus ps_publish(PsTopic topic, const void *payload, size_t size,
                    PsPublishReport *report)
{
    PsMessage message = {0};
    PsPublishReport result = {0};
    PsStatus status;
    size_t i;
    if (report != NULL) {
        *report = result;
    }
    if (payload == NULL || size == 0 || size > PS_MAX_PAYLOAD) {
        return PS_INVALID_ARGUMENT;
    }
    status = lock_bus(PS_WAIT_FOREVER);
    if (status != PS_OK) {
        return status;
    }
    if (topic >= topic_count) {
        unlock_bus();
        return PS_NOT_FOUND;
    }
    if (size != topics[topic].size) {
        unlock_bus();
        return PS_SIZE_MISMATCH;
    }
    message.topic = topic;
    message.size = (uint16_t)size;
    message.sequence = ++topics[topic].sequence;
    message.timestamp_ms = ps_port_now_ms();
    memcpy(message.data, payload, size);
    for (i = 0; i < client_count; ++i) {
        PsClient *client = &clients[i];
        if ((client->subscriptions & (UINT32_C(1) << topic)) == 0) {
            continue;
        }
        ++result.matched;
        if (ps_port_queue_try_send(client->queue, &message)) {
            ++result.delivered;
            ++client->stats.delivered;
            /* Set after enqueue, under the same mutex used to clear. */
            ps_port_event_set(client->events);
        } else {
            ++result.dropped;
            ++client->stats.dropped;
        }
    }
    unlock_bus();
    if (report != NULL) {
        *report = result;
    }
    return result.dropped == 0 ? PS_OK : PS_PARTIAL;
}

static uint32_t remaining_ms(uint64_t started, uint32_t timeout_ms)
{
    uint64_t elapsed;
    if (timeout_ms == PS_WAIT_FOREVER) {
        return PS_WAIT_FOREVER;
    }
    elapsed = ps_port_now_ms() - started;
    return elapsed >= timeout_ms ? 0 : timeout_ms - (uint32_t)elapsed;
}

PsStatus ps_receive(PsClient *client, PsMessage *message, uint32_t timeout_ms)
{
    uint64_t started;
    PsStatus status;
    if (client == NULL || message == NULL) {
        return PS_INVALID_ARGUMENT;
    }
    started = ps_port_now_ms();
    for (;;) {
        uint32_t remaining = remaining_ms(started, timeout_ms);
        status = lock_bus(remaining);
        if (status != PS_OK) {
            return status;
        }
        if (!valid_client(client)) {
            unlock_bus();
            return PS_INVALID_ARGUMENT;
        }
        if (ps_port_queue_try_receive(client->queue, message)) {
            ++client->stats.received;
            if (ps_port_queue_count(client->queue) == 0) {
                ps_port_event_clear(client->events);
            }
            unlock_bus();
            return PS_OK;
        }
        /* The same mutex protects enqueue + set and empty-check + clear. */
        ps_port_event_clear(client->events);
        unlock_bus();
        remaining = remaining_ms(started, timeout_ms);
        if (remaining == 0) {
            return PS_TIMEOUT;
        }
        /* Persistent level flag: a publish between unlock and wait cannot
         * lose its wakeup. Recheck the queue after capped/spurious waits too.
         */
        (void)ps_port_event_wait(client->events, remaining);
    }
}

PsStatus ps_client_stats(PsClient *client, PsClientStats *stats)
{
    PsStatus status;
    if (stats == NULL) {
        return PS_INVALID_ARGUMENT;
    }
    status = lock_bus(PS_WAIT_FOREVER);
    if (status != PS_OK) {
        return status;
    }
    if (valid_client(client) == true) {
        *stats = client->stats;
    } else {
        status = PS_INVALID_ARGUMENT;
    }
    unlock_bus();
    return status;
}
