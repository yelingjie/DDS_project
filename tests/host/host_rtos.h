#ifndef HOST_RTOS_H
#define HOST_RTOS_H
#include <stdint.h>
/* A Windows API shim for behavioral tests; not a FreeRTOS kernel port. */
void host_fail_after(int successful_allocations);
unsigned int host_live_allocations(void);
void host_before_event_wait(void (*hook)(void));
void host_advance_ticks(uint64_t ticks);
uint32_t host_last_event_wait_ticks(void);
#endif
