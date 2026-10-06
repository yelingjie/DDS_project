#ifndef HOST_SEMPHR_H
#define HOST_SEMPHR_H
#include "FreeRTOS.h"
typedef struct HostMutex *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t timeout);
BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex);
#endif
