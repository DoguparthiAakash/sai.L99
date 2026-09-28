/**
 * @file sai/kernel.h
 * @brief Kernel lifecycle, threading API and scheduler controls.
 *
 * Thread stacks: a thread created with @c stack == NULL and @c stack_size > 0
 * gets a dynamically allocated stack from the kernel heap; @c stack_size == 0
 * uses the build-time default (CONFIG_SAI_MAIN_STACK_SIZE / per-thread default).
 * Pass a static buffer for fully static operation.
 */
#ifndef SAI_KERNEL_H
#define SAI_KERNEL_H

#include <sai/types.h>
#include <sai/config.h>
#if CONFIG_SAI_BUDGET
#include <sai/budget.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Entry point for the kernel main thread.  Passed to sai_kernel_start();
 * returning from it performs an orderly kernel shutdown (on the host port,
 * the process exits with the test/return status; on targets, port_halt()).
 */
typedef void (*sai_main_fn_t)(void);
typedef enum {
    SAI_THREAD_DEAD     = 0,  /**< Not started or terminated.                */
    SAI_THREAD_READY    = 1,  /**< Runnable, waiting for CPU.                */
    SAI_THREAD_RUNNING  = 2,  /**< Currently executing.                      */
    SAI_THREAD_BLOCKED  = 3,  /**< Waiting on a kernel object.               */
    SAI_THREAD_SLEEPING = 4,  /**< Sleeping for a timed duration.            */
    SAI_THREAD_SUSPENDED= 5,  /**< Suspended by sai_thread_suspend().        */
    SAI_THREAD_JOINABLE = 6,  /**< Internal: joiner blocked on a target.     */
} sai_thread_state_t;

/* ------------------------------------------------------------------ */
/* Thread object                                                       */
/* ------------------------------------------------------------------ */
/** Thread entry function. Returning from it terminates the thread. */
typedef void (*sai_thread_entry_t)(void *arg);

/**
 * Thread control block. Applications should treat it as opaque and use the
 * API below; the fields are public so threads can be declared statically.
 */
typedef struct sai_thread {
    sai_kobj_t            kobj;
    struct sai_thread    *prio_next;     /**< Same-priority ready chain.      */
    struct sai_thread    *wq_next;       /**< Wait queue linkage.             */
    void                 *wq_wait;       /**< Wait queue this thread is on.   */
    uint32_t              wq_key;        /**< Per-waiter sort key (fifo/PRIO).*/
    uint32_t              wq_result;     /**< Wakeup result (internal).       */
    uint32_t              wq_flags_out;  /**< Event bits delivered on wake.   */
    uint8_t               wq_flags;      /**< Wait flag bits (internal).      */
    struct sai_thread    *join_next;     /**< Threads joined on us.           */

    sai_thread_entry_t    entry;         /**< Entry function.                 */
    void                 *arg;           /**< Entry argument.                 */

    void                 *stack_base;    /**< Allocated stack (may == stack). */
    size_t                stack_size;    /**< Stack size in bytes.            */
    void                 *sp;            /**< Saved stack pointer (arch).     */
    void                 *arch;          /**< Arch/port private context.      */

    uint8_t               prio;          /**< Current (effective) priority.   */
    uint8_t               base_prio;     /**< Base priority.                  */
    uint8_t               state;         /**< sai_thread_state_t.             */
    uint8_t               sched_mode;    /**< sai_sched_mode_t.               */
    uint16_t              flags;         /**< SAI_THREAD_FLAG_*.              */

    uint32_t              timeslice;     /**< Ticks remaining in this slice.  */
    uint32_t              wake_at;       /**< Absolute tick deadline.         */
    struct sai_thread    *deadline_next; /**< Kernel deadline-list linkage.   */
    struct sai_waitqueue *wq;            /**< Wait queue we are blocked on.   */

#if CONFIG_SAI_THREAD_STATS
    uint32_t              ctx_switches;
    uint32_t              last_ran_tick;
#endif
#if CONFIG_SAI_STACK_SENTINEL
    uint32_t              stack_sentinel;
#endif
    /* Mutex ownership chain for priority inheritance (internal). */
    struct sai_mutex     *held_mutexes;

#if CONFIG_SAI_BUDGET
    /* Latency-budget enforcement (see sai/budget.h). Kept at the tail so
     * existing TCB layouts (and arch offset asserts) are unaffected. */
    sai_budget_t          budget;           /**< Enforcement state.       */
    uint32_t              budget_run_start; /**< Clock at dispatch.       */
    uint8_t               budget_flags;     /**< SAI_TCB_BUDGET bit.      */
#endif
} sai_thread_t;

#if CONFIG_SAI_BUDGET
/** t->budget_flags bit: budget struct is initialized. */
#define SAI_TCB_BUDGET 0x01u
#endif

/** Thread creation flags. */
#define SAI_THREAD_COOPERATIVE   0x0001u  /**< Yield-only scheduling.        */

/* ------------------------------------------------------------------ */
/* Scheduler mode                                                      */
/* ------------------------------------------------------------------ */
typedef enum {
    SAI_SCHED_PREEMPTIVE   = 0,  /**< Preemptive, priority + timeslice.       */
    SAI_SCHED_COOPERATIVE  = 1,  /**< Switch only at yield/block points.      */
} sai_sched_mode_t;

/** Set the global scheduling mode (SAI_SCHED_PREEMPTIVE/SAI_SCHED_COOPERATIVE). */
void sai_sched_set_mode(sai_sched_mode_t mode);
/** Get the current scheduling mode. */
sai_sched_mode_t sai_sched_get_mode(void);

/** Enable/disable round-robin time slicing between equal-priority threads. */
void sai_sched_set_timeslice(uint32_t ticks);
/** Current round-robin timeslice in ticks. */
uint32_t sai_sched_get_timeslice(void);

/* ------------------------------------------------------------------ */
/* Thread API                                                          */
/* ------------------------------------------------------------------ */
/**
 * Create a thread.
 *
 * @param t          Thread object (static or dynamically allocated).
 * @param name       Static name for diagnostics (not copied).
 * @param entry      Entry function.
 * @param arg        Argument passed to @p entry.
 * @param prio       0..30 (31 reserved for the idle thread).
 * @param stack      Stack buffer, or NULL to allocate from the kernel heap.
 * @param stack_size Stack size in bytes (0 = kernel default).
 * @param flags      SAI_THREAD_* flags (0 for defaults).
 * @return SAI_OK, or a negative sai_status_t.
 */
sai_status_t sai_thread_create(sai_thread_t *t, const char *name,
                               sai_thread_entry_t entry, void *arg,
                               uint8_t prio, void *stack, size_t stack_size,
                               uint32_t flags);

/** Spawn a dynamically allocated thread (sai_thread_delete() to reclaim). */
sai_thread_t *sai_thread_spawn(const char *name, sai_thread_entry_t entry,
                               void *arg, uint8_t prio, size_t stack_size,
                               uint32_t flags);

/** Start a thread created with sai_thread_create() (initial state = suspended). */
sai_status_t sai_thread_start(sai_thread_t *t);

/** Terminate the calling thread immediately. */
SAI_NORETURN void sai_thread_exit(void);

/** Terminate another thread. */
sai_status_t sai_thread_delete(sai_thread_t *t);

/** Wait for a thread to terminate (releases resources when it exits). */

/** Request cooperative rescheduling of the calling thread. */
void sai_yield(void);

/** Sleep for @p ms milliseconds (rounded up to whole ticks). */
sai_status_t sai_sleep(uint32_t ms);

/** Sleep for @p ticks kernel ticks. */
sai_status_t sai_sleep_ticks(uint32_t ticks);

/** Put another thread to sleep for @p ms milliseconds. */
sai_status_t sai_thread_sleep(sai_thread_t *t, uint32_t ms);

/** Wake a thread that is sleeping or blocked with a timeout. */
sai_status_t sai_thread_wake(sai_thread_t *t);

/** Suspend a thread until sai_thread_resume() is called. */
sai_status_t sai_thread_suspend(sai_thread_t *t);

/** Resume a suspended thread. */
sai_status_t sai_thread_resume(sai_thread_t *t);

/** Wait for a thread to terminate. @p timeout_ms < 0 waits forever. */
sai_status_t sai_thread_join(sai_thread_t *t, int32_t timeout_ms);

/** Change a thread's base priority (priority inheritance may raise it). */
sai_status_t sai_thread_set_priority(sai_thread_t *t, uint8_t prio);

/** Current base priority of a thread. */
uint8_t sai_thread_get_priority(const sai_thread_t *t);

/** Current (inherited) priority of a thread. */
uint8_t sai_thread_get_effective_priority(const sai_thread_t *t);

/** The calling thread (NULL before the kernel is started). */
sai_thread_t *sai_current_thread(void);

/** Snapshot of a thread's state (thread-safe diagnostics). */
typedef struct {
    const char *name;
    uint8_t     prio;
    uint8_t     base_prio;
    uint8_t     state;       /**< sai_thread_state_t.  */
    uint8_t     sched_mode;
    uint32_t    stack_size;
    uint32_t    stack_used;  /**< High-water mark if CONFIG_SAI_STACK_SENTINEL. */
} sai_thread_info_t;

void sai_thread_get_info(const sai_thread_t *t, sai_thread_info_t *out);

/* ------------------------------------------------------------------ */
/* ISR-safe signaling (the only ISR API surface)                       */
/* ------------------------------------------------------------------ */
/**
 * Wake a thread from an interrupt handler. Never blocks, never calls the
 * scheduler: the pending reschedule happens at the tail of the ISR.
 * @return SAI_OK if a higher-priority thread was made ready (latency hint).
 */
sai_status_t sai_isr_wake(sai_thread_t *t);

/** ISR-safe variants of the non-blocking IPC primitives. */
struct sai_msgq;
struct sai_mbox;
struct sai_event;

sai_status_t sai_isr_msgq_put(struct sai_msgq *q, const void *msg);
sai_status_t sai_isr_mbox_put(struct sai_mbox *b, uint8_t byte);
sai_status_t sai_isr_event_set(struct sai_event *e, uint32_t flags);

/* ------------------------------------------------------------------ */
/* Kernel lifecycle                                                    */
/* ------------------------------------------------------------------ */
/** Initialize the kernel. Called by board startup before main(). */
sai_status_t sai_kernel_init(void);

/**
 * Start multitasking; never returns on success. On the ARM port this is
 * reached from main() after board init; on the host port the calling thread
 * becomes the kernel main thread.
 */
/**
 * Boot the scheduler.  @p main_fn becomes the entry of the "main" kernel
 * thread (priority 1); the idle thread runs at SAI_IDLE_PRIORITY.  This
 * never returns to the caller in the usual sense: control passes to
 * @p main_fn on the same native context (host port) or on PSP (ARM port).
 */
void sai_kernel_start(sai_main_fn_t main_fn);

/** True once sai_kernel_start() has switched to the idle task. */
bool sai_kernel_started(void);

/** Number of context switches since boot (diagnostics). */
uint32_t sai_ctx_switches(void);

/** Number of ISR-to-scheduler reschedule requests (diagnostics). */
uint32_t sai_isr_reschedules(void);

/* ------------------------------------------------------------------ */
/* Scheduler internals used by kernel modules (not a public API)       */
/* ------------------------------------------------------------------ */
void _sai_ready_thread(sai_thread_t *t);
void _sai_unready_thread(sai_thread_t *t);
sai_thread_t *_sai_pick_next(void);
void _sai_schedule(void);
void _sai_tick_handler(void);
void _sai_make_ready_locked(sai_thread_t *t);

#ifdef __cplusplus
}
#endif

#endif /* SAI_KERNEL_H */
