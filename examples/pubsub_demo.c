#include "pubsub_demo.h"

#include <string.h>
#ifdef ESP_PLATFORM
#include "freertos/task.h"
#include "freertos/event_groups.h"
#else
#include "task.h"
#include "event_groups.h"
#endif

#ifndef PS_DEMO_STACK_DEPTH
#ifdef ESP_PLATFORM
/* ESP-IDF xTaskCreate measures stack depth in bytes. */
#define PS_DEMO_STACK_DEPTH 4096
#else
#define PS_DEMO_STACK_DEPTH (configMINIMAL_STACK_SIZE + 256)
#endif
#endif
#ifndef PS_DEMO_PRIORITY
#define PS_DEMO_PRIORITY (tskIDLE_PRIORITY + 1)
#endif

typedef struct {
    char name;
    PsClient *client;
    PsTopic topics[3];
} DemoNode;

static DemoNode nodes[4];
static PsDemoMessageHandler message_handler;
static PsDemoPublishHandler publish_handler;
static bool started;
static EventGroupHandle_t start_gate;

static const char *const names[4][3] = {
    {"/a/1", "/a/2", "/a/3"},
    {"/b/1", "/b/2", "/b/3"},
    {"/c/1", "/c/2", "/c/3"},
    {"/d/1", "/d/2", "/d/3"}
};

static void node_task(void *argument)
{
    DemoNode *node = argument;
    uint32_t counter = 0;
    const TickType_t period = pdMS_TO_TICKS(1000);
    configASSERT(period > 0);
    (void)xEventGroupWaitBits(start_gate, (EventBits_t)1U, pdFALSE,
                             pdTRUE, portMAX_DELAY);
    for (;;) {
        TimeOut_t timer;
        TickType_t remaining = period;
        unsigned int child;
        /* One total period for publishing and receiving, even under traffic. */
        vTaskSetTimeOutState(&timer);
        ++counter;
        for (child = 0; child < 3; ++child) {
            PsDemoPayload payload;
            PsPublishReport report;
            PsStatus status;
            memset(&payload, 0, sizeof(payload)); /* Include struct padding. */
            payload.source = node->name;
            payload.child = (uint8_t)(child + 1);
            payload.counter = counter;
            payload.value = (int32_t)(child + 1);
            status = ps_publish(node->topics[child], &payload,
                                sizeof(payload), &report);
            if (publish_handler != NULL) {
                publish_handler(node->name, node->topics[child], status, &report);
            }
        }
        while (xTaskCheckForTimeOut(&timer, &remaining) == pdFALSE) {
            PsMessage message;
            /* The application uses native ticks; pubsub API uses ms. */
            uint32_t wait_ms = (uint32_t)(((uint64_t)remaining * 1000U +
                                          configTICK_RATE_HZ - 1U) /
                                         configTICK_RATE_HZ);
            PsStatus status = ps_receive(node->client, &message, wait_ms);
            if (status == PS_OK && message_handler != NULL) {
                message_handler(node->name, &message);
            } else if (status != PS_OK && status != PS_TIMEOUT) {
                configASSERT(status == PS_OK || status == PS_TIMEOUT);
                vTaskDelay(period);
                break;
            }
        }
    }
}

bool ps_demo_start(PsDemoMessageHandler on_message,
                        PsDemoPublishHandler on_publish)
{
    TaskHandle_t handles[4] = {NULL};
    static const char *const task_names[4] = {"TaskA", "TaskB", "TaskC", "TaskD"};
    unsigned int node;
    if (started) {
        return false;
    }
    started = true; /* Failures require fixing startup and restarting. */
    if (ps_init() != PS_OK) {
        return false;
    }
    for (node = 0; node < 4; ++node) {
        unsigned int child;
        nodes[node].name = (char)('a' + node);
        for (child = 0; child < 3; ++child) {
            if (ps_advertise(names[node][child], sizeof(PsDemoPayload),
                             &nodes[node].topics[child]) != PS_OK) {
                return false;
            }
        }
        if (ps_client_create(8, &nodes[node].client) != PS_OK) {
            return false;
        }
    }
    if (ps_subscribe(nodes[0].client, "/b/1") != PS_OK ||
        ps_subscribe(nodes[0].client, "/c/2") != PS_OK ||
        ps_subscribe(nodes[0].client, "/d/3") != PS_OK ||
        ps_subscribe(nodes[1].client, "/c/1") != PS_OK ||
        ps_subscribe(nodes[1].client, "/c/2") != PS_OK) {
        return false;
    }
    message_handler = on_message;
    publish_handler = on_publish;
    start_gate = xEventGroupCreate();
    if (start_gate == NULL) {
        return false;
    }
    for (node = 0; node < 4; ++node) {
        if (xTaskCreate(node_task, task_names[node], PS_DEMO_STACK_DEPTH,
                        &nodes[node], PS_DEMO_PRIORITY, &handles[node]) != pdPASS) {
            while (node > 0) {
                vTaskDelete(handles[--node]);
            }
            return false;
        }
    }
    (void)xEventGroupSetBits(start_gate, (EventBits_t)1U);
    return true;
}
