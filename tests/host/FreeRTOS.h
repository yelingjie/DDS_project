#ifndef HOST_FREERTOS_H
#define HOST_FREERTOS_H
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
#ifdef HOST_TICK_16
typedef uint16_t TickType_t;
#define portMAX_DELAY UINT16_MAX
#else
typedef uint32_t TickType_t;
#define portMAX_DELAY UINT32_MAX
#endif
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#ifndef HOST_TICK_HZ
#define HOST_TICK_HZ 1000
#endif
#define configTICK_RATE_HZ HOST_TICK_HZ
#define pdMS_TO_TICKS(ms) ((TickType_t)((uint64_t)(ms) * configTICK_RATE_HZ / 1000U))
#define configUSE_MUTEXES 1
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configMINIMAL_STACK_SIZE 128
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_vTaskDelete 1
#define configASSERT(x) assert(x)
#endif
