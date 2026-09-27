/**
 * @file sai/host.h
 * @brief Host-port control API (arch/host).
 *
 * Lets tests and samples drive the simulated interrupt machinery, install
 * exit handlers, and read port internals. Only available in host builds.
 */
#ifndef SAI_HOST_H
#define SAI_HOST_H

#include <sai/types.h>
#include <sai/config.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Simulate an interrupt: run the registered handler for @p irq, then perform
 * the ISR tail (deferred reschedule). Never call from a SAI thread.
 */
void sai_host_raise_isr(uint32_t irq);

/** Register (or replace) a simulated ISR handler for @p irq. */
typedef void (*sai_host_isr_t)(void *arg);
void sai_host_isr_register(uint32_t irq, sai_host_isr_t fn, void *arg);

/**
 * Initialize host port internals (timer thread, main thread context).
 * Called automatically by sai_kernel_init(); exposed for early tests.
 */
void sai_host_port_init(void);

/** Stop the host port timer thread (used by tests before process exit). */
void sai_host_shutdown(void);

/**
 * Advance the tick synchronously without the timer thread (deterministic
 * tests); the timer thread stays running for wall-clock time.
 */
void sai_host_advance_tick_sync(uint32_t ticks);

/** Install an at-exit hook that also stops the timer thread. */
void sai_host_atexit(void);

/**
 * Deterministic tick source: advance the kernel tick by one (bypasses the
 * real timer thread). Used by unit tests to test timeouts synchronously.
 */
void sai_host_advance_tick(uint32_t ticks);

/** Number of simulated ISRs executed (diagnostics). */
uint32_t sai_host_isr_count(void);

#ifdef __cplusplus
}
#endif

#endif /* SAI_HOST_H */
