#ifndef PUBSUB_DEMO_H
#define PUBSUB_DEMO_H

#include "pubsub.h"
#include <stdbool.h>

typedef struct {
    char source;             /* 'a', 'b', 'c', 'd' */
    uint8_t child;           /* 1, 2, 3 */
    uint32_t counter;
    int32_t value;
} PsDemoPayload;

/* Called in the receiving/publishing task. Keep handlers short; synchronize
 * shared peripherals (e.g. UART) yourself. Message/report pointers are only
 * valid during the call. NULL handlers are allowed.
 */
typedef void (*PsDemoMessageHandler)(char receiver, const PsMessage *message);
typedef void (*PsDemoPublishHandler)(char publisher, PsTopic topic,
                                     PsStatus status,
                                     const PsPublishReport *report);

/* Call ONCE from startup (or ESP-IDF app_main). All tasks wait at a startup
 * gate until creation completes. false means startup failed; restart after
 * correcting the cause, as pubsub clients have application lifetime.
 * Set INCLUDE_vTaskDelete=1 for cleanup if a task creation fails.
 */
bool ps_demo_start(PsDemoMessageHandler on_message,
                        PsDemoPublishHandler on_publish);

#endif
