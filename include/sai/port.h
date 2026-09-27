/**
 * @file sai/port.h
 * @brief Architecture porting contract (the kernel <-> arch seam).
 *
 * The portable kernel core calls ONLY the functions declared here; every
 * arch/<name>/ port implements them. This is the single seam between the
 * kernel and the CPU/platform.
 */
#ifndef SAI_PORT_H
#define SAI_PORT_H

#include <sai/types.h>
#include <sai/config.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Critical sections / interrupt control                               */
/* ------------------------------------------------------------------ */

/** Enter a critical section; returns an opaque saved state token. */
uint32_t port_lock(void);

/** Exit a critical section, restoring the token from port_lock(). */
void port_unlock(uint32_t key);

/** Spinlock backends (ticket word in l->slock). */
uint32_t port_spin_lock(volatile uint32_t *slock);
uint32_t port_spin_unlock(volatile uint32_t *slock, uint32_t saved);

/** True when executing in interrupt context. */
bool port_in_isr(void);

/* ------------------------------------------------------------------ */
/* Context switching                                                   */
/* ------------------------------------------------------------------ */

/**
 * Prepare a thread's initial stack so that it will begin executing
 * entry(arg) when first switched in. Returns the new stack pointer value
 * (the value to store in sai_thread_t.sp).
 */
void *port_stack_init(void *stack_top, size_t stack_size,
                      sai_thread_entry_t entry, void *arg);

/**
 * Perform a context switch: save the current context into
 * _sai_current->sp and restore the context from next->sp.
 * Called with interrupts disabled by the scheduler.
 */
void port_switch(sai_thread_t *from, sai_thread_t *to);

/** Request a switch from interrupt context (reschedule tail). */
void port_schedule_from_isr(void);

/* ------------------------------------------------------------------ */
/* Time / tick                                                         */
/* ------------------------------------------------------------------ */

/**
 * Configure the tick source: period_ms tick, or one-shot compare mode when
 * tickless is enabled (re-arm via port_timer_oneshot()).
 */
void port_timer_setup(uint32_t period_ms);

/**
 * Tickless idle: program a one-shot timer to fire after the given number of
 * ticks (the kernel compensates elapsed time on wake).
 */
void port_timer_oneshot(uint32_t ticks);

/** Enter low-power idle until the next tick/interrupt (may be a no-op). */
void port_idle_until_tick(void);

/** Read a free-running high-resolution counter (ticks of SAI_TICK_MS). */
uint32_t port_cycle_count(void);

/* ------------------------------------------------------------------ */
/* Scheduler / kernel hooks                                            */
/* ------------------------------------------------------------------ */

/**
 * Prepare a thread's native execution context (called at first start).
 * On the host port this spawns the native thread; on ARM it initializes the
 * saved stack frame.  @p t->sp must be set afterwards (or by this call).
 */
void port_thread_ready(sai_thread_t *t);

/** Called once during sai_kernel_init(), before threads exist. */
void port_init(void);

/** Start the first thread (never returns on target; see arch notes). */
void port_start_first_thread(sai_thread_t *t);

/**
 * Called when the main thread's function returns: orderly kernel shutdown.
 * On the host this stops the supervisor and exits the process with the
 * test status; on a target it typically calls port_halt().
 */
void port_main_returned(void);

/** Idle-loop hook for the idle thread (usually wfi or spin). */
void port_idle(void);

/** Flush/drain any pending deferred work before the idle loop spins. */
void port_idle_wait_for_interrupt(void);

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

/** Low-level character output (polled; usable from panic context). */
void port_putchar(char c);

/** Optional architectural backtrace on panic (may be a no-op). */
void port_backtrace(void);

/** Force a hard halt (never returns). */
SAI_NORETURN void port_halt(void);

/* ------------------------------------------------------------------ */
/* Stack metadata (for guard/sentinel setup)                           */
/* ------------------------------------------------------------------ */

/** Stack alignment in bytes for this architecture. */
#define SAI_STACK_ALIGN 8u

/**
 * True when the stack grows downward on this architecture (the common case;
 * only exotic ports would define this to 0).
 */
#ifndef SAI_STACK_GROWS_DOWN
#define SAI_STACK_GROWS_DOWN 1
#endif

#ifdef __cplusplus
}
#endif

#endif /* SAI_PORT_H */
