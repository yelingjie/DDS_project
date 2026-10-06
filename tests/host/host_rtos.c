#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "queue.h"
#include "event_groups.h"
#include "semphr.h"
#include "task.h"
#include "host_rtos.h"

struct HostMutex { HANDLE handle; };
struct HostQueue {
    CRITICAL_SECTION lock;
    UBaseType_t depth, item_size, count, head;
    unsigned char data[];
};
struct HostEvent {
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE changed;
    EventBits_t bits;
};

static int fail_after = -1;
static unsigned int live_allocations;
static void (*wait_hook)(void);
static uint64_t tick_offset;
static volatile LONG last_event_wait_ticks;

void host_advance_ticks(uint64_t ticks) { tick_offset += ticks; }
uint32_t host_last_event_wait_ticks(void)
{
    return (uint32_t)InterlockedCompareExchange(&last_event_wait_ticks, 0, 0);
}

static uint64_t native_ticks(void)
{
    return GetTickCount64() * configTICK_RATE_HZ / 1000U + tick_offset;
}

static DWORD native_wait_ms(TickType_t timeout)
{
    return timeout == portMAX_DELAY ? INFINITE :
        (DWORD)(((uint64_t)timeout * 1000U + configTICK_RATE_HZ - 1U) /
                configTICK_RATE_HZ);
}

void host_fail_after(int count) { fail_after = count; }
unsigned int host_live_allocations(void) { return live_allocations; }
void host_before_event_wait(void (*hook)(void)) { wait_hook = hook; }

static void *host_alloc(size_t size)
{
    void *memory;
    if (fail_after == 0) {
        return NULL;
    }
    if (fail_after > 0) {
        --fail_after;
    }
    memory = calloc(1, size);
    if (memory != NULL) {
        ++live_allocations;
    }
    return memory;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    SemaphoreHandle_t mutex = host_alloc(sizeof(*mutex));
    if (mutex != NULL) {
        mutex->handle = CreateMutex(NULL, FALSE, NULL);
        assert(mutex->handle != NULL);
    }
    return mutex;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t timeout)
{
    return WaitForSingleObject(mutex->handle, native_wait_ms(timeout)) == WAIT_OBJECT_0;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex)
{
    return ReleaseMutex(mutex->handle) != 0;
}

QueueHandle_t xQueueCreate(UBaseType_t depth, UBaseType_t item_size)
{
    QueueHandle_t queue = host_alloc(sizeof(*queue) + (size_t)depth * item_size);
    if (queue != NULL) {
        InitializeCriticalSection(&queue->lock);
        queue->depth = depth;
        queue->item_size = item_size;
    }
    return queue;
}

void vQueueDelete(QueueHandle_t queue)
{
    DeleteCriticalSection(&queue->lock);
    free(queue);
    --live_allocations;
}

BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t timeout)
{
    BaseType_t result = pdFAIL;
    assert(timeout == 0);
    EnterCriticalSection(&queue->lock);
    if (queue->count < queue->depth) {
        UBaseType_t tail = (queue->head + queue->count) % queue->depth;
        memcpy(queue->data + (size_t)tail * queue->item_size, item, queue->item_size);
        ++queue->count;
        result = pdPASS;
    }
    LeaveCriticalSection(&queue->lock);
    return result;
}

BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t timeout)
{
    BaseType_t result = pdFAIL;
    assert(timeout == 0);
    EnterCriticalSection(&queue->lock);
    if (queue->count != 0) {
        memcpy(item, queue->data + (size_t)queue->head * queue->item_size,
               queue->item_size);
        queue->head = (queue->head + 1) % queue->depth;
        --queue->count;
        result = pdPASS;
    }
    LeaveCriticalSection(&queue->lock);
    return result;
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue)
{
    UBaseType_t count;
    EnterCriticalSection(&queue->lock);
    count = queue->count;
    LeaveCriticalSection(&queue->lock);
    return count;
}

EventGroupHandle_t xEventGroupCreate(void)
{
    EventGroupHandle_t event = host_alloc(sizeof(*event));
    if (event != NULL) {
        InitializeCriticalSection(&event->lock);
        InitializeConditionVariable(&event->changed);
    }
    return event;
}

EventBits_t xEventGroupSetBits(EventGroupHandle_t event, EventBits_t bits)
{
    EventBits_t result;
    EnterCriticalSection(&event->lock);
    event->bits |= bits;
    result = event->bits;
    WakeAllConditionVariable(&event->changed);
    LeaveCriticalSection(&event->lock);
    return result;
}

EventBits_t xEventGroupClearBits(EventGroupHandle_t event, EventBits_t bits)
{
    EventBits_t result;
    EnterCriticalSection(&event->lock);
    result = event->bits;
    event->bits &= ~bits;
    LeaveCriticalSection(&event->lock);
    return result;
}

EventBits_t xEventGroupWaitBits(EventGroupHandle_t event, EventBits_t bits,
                               BaseType_t clear, BaseType_t all,
                               TickType_t timeout)
{
    ULONGLONG start = GetTickCount64();
    DWORD timeout_ms = native_wait_ms(timeout);
    EventBits_t result;
    InterlockedExchange(&last_event_wait_ticks, (LONG)timeout);
    if (wait_hook != NULL) {
        void (*hook)(void) = wait_hook;
        wait_hook = NULL;
        hook(); /* Deterministic publish in the queue-check / wait gap. */
    }
    EnterCriticalSection(&event->lock);
    for (;;) {
        EventBits_t matched = event->bits & bits;
        ULONGLONG elapsed = GetTickCount64() - start;
        DWORD wait;
        if ((all && matched == bits) || (!all && matched != 0)) {
            break;
        }
        if (timeout_ms != INFINITE && elapsed >= timeout_ms) {
            break;
        }
        wait = timeout_ms == INFINITE ? INFINITE : (DWORD)(timeout_ms - elapsed);
        (void)SleepConditionVariableCS(&event->changed, &event->lock, wait);
    }
    result = event->bits;
    if (clear && ((all && (result & bits) == bits) || (!all && (result & bits)))) {
        event->bits &= ~bits;
    }
    LeaveCriticalSection(&event->lock);
    return result;
}

TickType_t xTaskGetTickCount(void) { return (TickType_t)native_ticks(); }
void vTaskSetTimeOutState(TimeOut_t *timer)
{
    uint64_t ticks = native_ticks();
    timer->start = ticks;
    timer->xTimeOnEntering = (TickType_t)ticks;
    timer->xOverflowCount = (BaseType_t)(ticks >> (sizeof(TickType_t) * 8));
}
BaseType_t xTaskCheckForTimeOut(TimeOut_t *timer, TickType_t *remaining)
{
    uint64_t now = native_ticks();
    uint64_t elapsed = now - timer->start;
    timer->start = now;
    if (*remaining == portMAX_DELAY) {
        return pdFALSE;
    }
    if (elapsed >= *remaining) {
        *remaining = 0;
        return pdTRUE;
    }
    *remaining -= (TickType_t)elapsed;
    return pdFALSE;
}
