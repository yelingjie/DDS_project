#ifndef HOST_TASK_H
#define HOST_TASK_H
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
typedef struct {
    BaseType_t xOverflowCount;
    TickType_t xTimeOnEntering;
    uint64_t start;
} TimeOut_t;
#define tskIDLE_PRIORITY 0
TickType_t xTaskGetTickCount(void);
void vTaskSetTimeOutState(TimeOut_t *timer);
BaseType_t xTaskCheckForTimeOut(TimeOut_t *timer, TickType_t *remaining);
BaseType_t xTaskCreate(void (*entry)(void *), const char *name,
                       uint32_t depth, void *arg, UBaseType_t priority,
                       TaskHandle_t *handle);
void vTaskDelete(TaskHandle_t task);
void vTaskDelay(TickType_t ticks);
void vTaskStartScheduler(void);
#endif
