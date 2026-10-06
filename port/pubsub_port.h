#ifndef PUBSUB_PORT_H
#define PUBSUB_PORT_H

/* Port implementers include this header. Application code includes pubsub.h.
 * No operating-system types or headers belong in either public interface.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS_PORT_WAIT_FOREVER UINT32_MAX

typedef struct PsPortMutexHandle *PsPortMutex;
typedef struct PsPortQueueHandle *PsPortQueue;
typedef struct PsPortEventHandle *PsPortEvent;

/* Thread-safe primitives, safe between cores. Creation returns NULL on
 * failure. Mutex unlock and event set/clear must succeed for valid handles.
 * No ISR calls. Startup creation before a scheduler is port-dependent.
 */
PsPortMutex ps_port_mutex_create(void);
bool ps_port_mutex_lock(PsPortMutex mutex, uint32_t timeout_ms);
void ps_port_mutex_unlock(PsPortMutex mutex);

/* Fixed-item FIFO; send/receive are nonblocking and copy item_size bytes.
 * Failed send/receive leaves the queue unchanged. Count is a snapshot.
 */
PsPortQueue ps_port_queue_create(uint32_t depth, size_t item_size);
void ps_port_queue_destroy(PsPortQueue queue);
bool ps_port_queue_try_send(PsPortQueue queue, const void *item);
bool ps_port_queue_try_receive(PsPortQueue queue, void *item);
uint32_t ps_port_queue_count(PsPortQueue queue);

/* A persistent level flag, initially clear; not a counting semaphore.
 * Set wakes a waiter. Wait does NOT clear the flag. Repeated set coalesces.
 * Finite waits may return early (e.g. native maximum wait length); the core
 * checks its total deadline and retries. FOREVER must actually wait forever.
 */
PsPortEvent ps_port_event_create(void);
void ps_port_event_set(PsPortEvent event);
void ps_port_event_clear(PsPortEvent event);
bool ps_port_event_wait(PsPortEvent event, uint32_t timeout_ms);

/* Thread-safe monotonic 64-bit milliseconds, no reset/native tick wrap.
 * Resolution may follow the OS tick. Must not allocate or acquire bus_lock.
 */
uint64_t ps_port_now_ms(void);

#ifdef __cplusplus
}
#endif
#endif
