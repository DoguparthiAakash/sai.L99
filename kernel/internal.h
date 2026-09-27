/**
 * @file kernel/internal.h
 * @brief Kernel-internal declarations shared across kernel/ modules.
 *
 * Not part of the public API.
 */
#ifndef SAI_INTERNAL_H
#define SAI_INTERNAL_H

#include <sai/types.h>
#include <sai/kernel.h>
#include <sai/time.h>
#include <sai/log.h>
#include <sai/port.h>
#include <sai/host.h>
#include <sai/sync.h>
#include <sai/ipc.h>
#include <sai/mem.h>

/* libc surface used across the kernel (our shims on freestanding targets,
 * the host CRT on Windows/Linux builds). */
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Wait queues                                                         */
/* ------------------------------------------------------------------ */
/** Per-thread wait flags. */
#define SAI_WQF_TIMED    0x01u  /* On the timeout list.                  */
#define SAI_WQF_CONSUME  0x02u  /* Event waiter: consume-on-exit.        */
#define SAI_WQF_ANY      0x04u  /* Event waiter: any-flag semantics.     */

/** Wakeup result codes delivered to waiters (stored in wq_result on wake). */
#define SAI_WQ_KEY_MASK    0x0FFFFFFFu

/** Waitqueue flags (type defined in sai/sync.h). */
#define WQ_FIFO   0x1u   /* FIFO order regardless of key.      */
#define WQ_PRIO   0x2u   /* Key = thread priority (condvar).   */
#define WQ_EVENT  0x4u   /* Event flags: key = requested mask. */
#define WQ_LOCK   0x8u   /* Mutex waiters: key = priority.     */
#define WQ_DYNAMIC 0x10u  /* Heap-allocated waitqueue.          */

/**
 * Block the calling thread on @p wq with optional timeout.
 * @param key sort key (priority or mask, depending on wq->flags).
 * @param timeout_ms SAI_NO_WAIT / SAI_WAIT_FOREVER / milliseconds.
 * @return SAI_OK (signaled), SAI_ERR_TIMEOUT, or SAI_ERR_WOULD_BLOCK
 *         (would have blocked with SAI_NO_WAIT; thread NOT enqueued).
 */
sai_status_t _sai_wq_block(struct sai_waitqueue *wq, uint32_t key, int32_t timeout_ms);

/** Remove a thread from its wait queue (if any); returns true if removed. */
bool _sai_wq_remove(sai_thread_t *t);

/** Internal: called by timeout expiry. */
void _sai_wq_timeout(sai_thread_t *t);

/**
 * Unblock a thread wherever it is waiting: remove from its wait queue and
 * the deadline list, deliver @p result through wq_result, and make it ready.
 * Safe to call for threads not on any queue (then it just readies them).
 */
void _sai_unblock_thread(sai_thread_t *t, sai_status_t result);

/* ------------------------------------------------------------------ */
/* Tick / deadline list                                                */
/* ------------------------------------------------------------------ */
extern volatile uint32_t _sai_tick_count;

/** Register a thread deadline (sleep or blocked-with-timeout), sorted by wake_at. */
void _sai_deadline_add(sai_thread_t *t);
/** Remove a thread deadline (on wake/destroy). */
void _sai_deadline_remove(sai_thread_t *t);
/** Expire due deadlines; makes threads ready (called from tick). */
void _sai_deadlines_tick(void);

/** Head of the deadline list (for tickless idle). */
sai_thread_t *_sai_deadline_head(void);

/** Next deadline in ticks for tickless idle (0 = none pending). */
uint32_t _sai_next_deadline_ticks(void);

/** Called from the tick ISR in one-shot mode to program the next wake. */
void _sai_tickless_rearm(void);

/* ------------------------------------------------------------------ */
/* Scheduler internals                                                 */
/* ------------------------------------------------------------------ */
/** Readiness bitmap; bit n set = a thread at priority n is ready. */
extern volatile uint32_t _sai_ready_bitmap;
/** Ready list heads, one per priority. */
extern sai_thread_t *_sai_ready[SAI_NUM_PRIORITIES];
/** The running thread (NULL until the kernel starts). */
extern sai_thread_t *_sai_current;
/** Round-robin timeslice in ticks. */
extern uint32_t _sai_timeslice;
/** Set once sai_kernel_start() has run. */
extern volatile bool _sai_started;
/** Global scheduling mode. */
extern uint8_t _sai_sched_mode;
/** Preemption hint set by wake paths; serviced at unlock tail. */
extern volatile bool _sai_preempt_hint;
/** Round-robin rotation request: priority ring to rotate at the next pick. */
extern volatile uint8_t _sai_rotate_prio;

/** Clear a thread's ready/running state and drop it from the ready list. */
void _sai_remove_ready(sai_thread_t *t);

/** Ready-ring operations shared with thread.c. */
void _sai_ready_insert(sai_thread_t *t);
void _sai_ready_remove(sai_thread_t *t);
/** Highest-priority ready thread (peek only), or NULL. */
sai_thread_t *_sai_ready_head(void);

/** Zombie reaping (dynamic TCBs/stacks freed by the idle thread). */
extern sai_thread_t *_sai_zombies;
void _sai_zombie_reap(void);

/** Put a thread to sleep for ticks (must not be ready). */
sai_status_t _sai_thread_sleep_ticks(sai_thread_t *t, uint32_t ticks);

/** Wake a sleeping/blocked-with-timeout thread now (timeout equivalent). */
sai_status_t _sai_thread_wake_now(sai_thread_t *t);

/** Timer expiry for a sleeping thread. */
void _sai_thread_sleep_expired(sai_thread_t *t);

/** Join bookkeeping. */
void _sai_thread_notify_joiners(sai_thread_t *t);

/** Tick bookkeeping for the running thread (timeslice accounting). */
void _sai_sched_tick(void);

/** Exit path shared by sai_thread_exit() and thread-return trampoline. */
SAI_NORETURN void _sai_thread_cleanup_and_exit(void);

/* ------------------------------------------------------------------ */
/* Mutex/PI internals                                                  */
/* ------------------------------------------------------------------ */
void _sai_mutex_pi_block(sai_mutex_t *m, sai_thread_t *waiter);
void _sai_mutex_pi_unblock(sai_mutex_t *m, sai_thread_t *waiter);

/* ------------------------------------------------------------------ */
/* Kernel object registry (diagnostics)                                */
/* ------------------------------------------------------------------ */
void _sai_kobj_register(sai_kobj_t *k, uint16_t type, const char *name);
void _sai_kobj_unregister(sai_kobj_t *k);
uint32_t _sai_kobj_count(uint16_t type);
void _sai_kobj_register_full(sai_kobj_t *k, uint16_t type, const char *name);
void _sai_kobj_unregister_full(sai_kobj_t *k);

/* ------------------------------------------------------------------ */
/* Stats                                                               */
/* ------------------------------------------------------------------ */
extern volatile uint32_t _sai_ctx_switches;
extern volatile uint32_t _sai_isr_reschedules;

/* ------------------------------------------------------------------ */
/* Main/idle thread bookkeeping                                        */
/* ------------------------------------------------------------------ */
extern sai_thread_t _sai_idle_thread;
extern sai_thread_t _sai_main_thread;
extern uint8_t      _sai_idle_stack[CONFIG_SAI_IDLE_STACK_SIZE];

/** Thread flags (internal). */
#define SAI_THREAD_FLAG_DYNAMIC   0x0001u /* Dynamically allocated TCB.  */
#define SAI_THREAD_FLAG_DYING     0x0002u /* Delete requested.           */
#define SAI_THREAD_FLAG_JOINED    0x0004u /* join() already consumed.    */
#define SAI_THREAD_FLAG_EXITED    0x0008u /* Ran to completion.          */
#define SAI_THREAD_FLAG_STACK_DYN 0x0010u /* Dynamically allocated stack.*/

/** Default thread stack size when none is given. */
#ifndef SAI_THREAD_STACK_DEFAULT
#define SAI_THREAD_STACK_DEFAULT CONFIG_SAI_MAIN_STACK_SIZE
#endif

/** Fill a thread's stack with the sentinel pattern (if enabled). */
void _sai_stack_poison(sai_thread_t *t);
/** Compute stack high-water mark in bytes (0 if disabled). */
uint32_t _sai_stack_used(const sai_thread_t *t);

/* Panic support: format + print reason from any context. */
void _sai_panic_print(const char *reason, const char *file, int line);

#ifdef __cplusplus
}
#endif

/* ------------------------------------------------------------------ */
/* Internal cross-module symbols                                       */
/* ------------------------------------------------------------------ */
extern sai_thread_t *_sai_current;
extern sai_thread_t  _sai_idle_thread;
extern sai_thread_t  _sai_main_thread;
extern volatile bool _sai_started;

void _sai_thread_cleanup_and_exit(void);
void _sai_zombie_reap(void);
void _sai_tick_handler(void);

/**
 * Single scheduling decision point: select the next thread, requeue the
 * previous runner if displaced, update _sai_current.  Called with interrupts
 * disabled; the caller performs the context switch itself.
 */
sai_thread_t *_sai_schedule_pick(void);

/** Cooperative reschedule from a thread (blocks until our turn returns). */
void _sai_schedule(void);

#endif /* SAI_INTERNAL_H */
