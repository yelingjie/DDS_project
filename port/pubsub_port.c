/* Default FreeRTOS adapter. Replace this file for another operating system.
 * pubsub_port.h and the core/API stay unchanged.
 */
#include "pubsub_port.h"
#include <limits.h>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#else
#include "FreeRTOS.h"
#include "event_groups.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#endif

#if configUSE_MUTEXES != 1 || configSUPPORT_DYNAMIC_ALLOCATION != 1
#error Pubsub_port_requires_mutexes_and_dynamic_allocation
#endif
#if INCLUDE_vTaskSuspend != 1
#error Pubsub_port_requires_INCLUDE_vTaskSuspend_for_infinite_waits
#endif

/* This adapter supports the 16/32-bit FreeRTOS tick formats. */
typedef char PsPortTickWidthCheck[(sizeof(TickType_t) <= 4) ? 1 : -1];
#define PS_PORT_READY ((EventBits_t)1U)

static TickType_t wait_ticks(uint32_t timeout_ms)
{
    uint64_t ticks;
    if (timeout_ms == PS_PORT_WAIT_FOREVER) {
        return portMAX_DELAY;
    }
    /* Round positive durations UP, and reserve portMAX_DELAY for infinity.
     * The core retries capped waits using its millisecond deadline.
     */
    ticks = ((uint64_t)timeout_ms * configTICK_RATE_HZ + 999U) / 1000U;
    if (ticks >= (uint64_t)portMAX_DELAY) {
        ticks = (uint64_t)portMAX_DELAY - 1U;
    }
    return (TickType_t)ticks;
}

PsPortMutex ps_port_mutex_create(void)
{
    return (PsPortMutex)xSemaphoreCreateMutex();
}

bool ps_port_mutex_lock(PsPortMutex mutex, uint32_t timeout_ms)
{
    uint64_t started = ps_port_now_ms();
    uint32_t remaining = timeout_ms;
    for (;;) {
        if (xSemaphoreTake((SemaphoreHandle_t)mutex, wait_ticks(remaining)) == pdTRUE) {
            return true;
        }
        if (timeout_ms != PS_PORT_WAIT_FOREVER) {
            uint64_t elapsed = ps_port_now_ms() - started;
            if (elapsed >= timeout_ms) {
                return false;
            }
            remaining = timeout_ms - (uint32_t)elapsed;
        }
    }
}

void ps_port_mutex_unlock(PsPortMutex mutex)
{
    (void)xSemaphoreGive((SemaphoreHandle_t)mutex);
}

PsPortQueue ps_port_queue_create(uint32_t depth, size_t item_size)
{
    if (depth == 0 || item_size == 0 || item_size > SIZE_MAX / depth ||
        (uint32_t)(UBaseType_t)depth != depth ||
        (size_t)(UBaseType_t)item_size != item_size) {
        return NULL;
    }
    return (PsPortQueue)xQueueCreate((UBaseType_t)depth, (UBaseType_t)item_size);
}

void ps_port_queue_destroy(PsPortQueue queue)
{
    vQueueDelete((QueueHandle_t)queue);
}

bool ps_port_queue_try_send(PsPortQueue queue, const void *item)
{
    return xQueueSend((QueueHandle_t)queue, item, 0) == pdPASS;
}

bool ps_port_queue_try_receive(PsPortQueue queue, void *item)
{
    return xQueueReceive((QueueHandle_t)queue, item, 0) == pdPASS;
}

uint32_t ps_port_queue_count(PsPortQueue queue)
{
    return (uint32_t)uxQueueMessagesWaiting((QueueHandle_t)queue);
}

PsPortEvent ps_port_event_create(void)
{
    return (PsPortEvent)xEventGroupCreate();
}

void ps_port_event_set(PsPortEvent event)
{
    (void)xEventGroupSetBits((EventGroupHandle_t)event, PS_PORT_READY);
}

void ps_port_event_clear(PsPortEvent event)
{
    (void)xEventGroupClearBits((EventGroupHandle_t)event, PS_PORT_READY);
}

bool ps_port_event_wait(PsPortEvent event, uint32_t timeout_ms)
{
    return (xEventGroupWaitBits((EventGroupHandle_t)event, PS_PORT_READY,
                               pdFALSE, pdFALSE, wait_ticks(timeout_ms)) &
            PS_PORT_READY) != 0;
}

uint64_t ps_port_now_ms(void)
{
    TimeOut_t snapshot;
    uint64_t ticks;
    /* Snapshot tick and overflow count atomically inside the kernel. Reading
     * just xTaskGetTickCount would reset time at a native tick wrap.
     */
    vTaskSetTimeOutState(&snapshot);
    ticks = (uint64_t)(UBaseType_t)snapshot.xOverflowCount;
    ticks <<= sizeof(TickType_t) * CHAR_BIT;
    ticks |= (uint64_t)snapshot.xTimeOnEntering;
    /* Divide first to avoid overflowing ticks * 1000 over long runtimes. */
    return (ticks / configTICK_RATE_HZ) * 1000U +
           ((ticks % configTICK_RATE_HZ) * 1000U) / configTICK_RATE_HZ;
}
