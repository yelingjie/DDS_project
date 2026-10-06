#ifndef HOST_QUEUE_H
#define HOST_QUEUE_H
#include "FreeRTOS.h"
typedef struct HostQueue *QueueHandle_t;
QueueHandle_t xQueueCreate(UBaseType_t depth, UBaseType_t item_size);
void vQueueDelete(QueueHandle_t queue);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t timeout);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t timeout);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue);
#endif
