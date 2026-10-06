#ifndef HOST_EVENT_GROUPS_H
#define HOST_EVENT_GROUPS_H
#include "FreeRTOS.h"
typedef uint32_t EventBits_t;
typedef struct HostEvent *EventGroupHandle_t;
EventGroupHandle_t xEventGroupCreate(void);
EventBits_t xEventGroupSetBits(EventGroupHandle_t event, EventBits_t bits);
EventBits_t xEventGroupClearBits(EventGroupHandle_t event, EventBits_t bits);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t event, EventBits_t bits,
                               BaseType_t clear, BaseType_t all,
                               TickType_t timeout);
#endif
