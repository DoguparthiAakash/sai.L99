/**
 * @file sai/budget.h
 * @brief Per-thread latency budget enforcement (the differentiator: declare
 *        WCET-style budgets; the kernel detects overruns and acts).
 *
 * A budget is a per-thread CPU-time allowance in ticks. The kernel accounts
 * actual runtime at every context switch and every tick; when a thread
 * exceeds its allowance within its accounting window the configured action
 * fires. Because the clock is injectable, the whole mechanism is
 * deterministically testable on the host (fake clock), and the same
 * accounting later feeds tracing / digital-twin replay.
 */
#ifndef SAI_BUDGET_H
#define SAI_BUDGET_H

#include <sai/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct sai_thread;

/** Action taken when a thread exceeds its budget. */
typedef enum {
    SAI_BUDGET_ACTION_LOG = 0,   /**< Log a warning (default).              */
    SAI_BUDGET_ACTION_DEMOTE,    /**< Drop the thread to a floor priority.  */
    SAI_BUDGET_ACTION_KILL,      /**< Terminate the offending thread.       */
} sai_budget_action_t;

/** Per-thread runtime accounting + configured budget. */
typedef struct sai_budget {
    uint32_t window_ticks;     /**< Accounting window (ticks; 0 = disabled). */
    uint32_t allowance_ticks;  /**< Max CPU ticks per window.                */
    uint32_t used_ticks;       /**< Runtime consumed in the current window.  */
    uint32_t window_start;     /**< Tick the current window began.           */
    uint32_t violations;       /**< Total violations since boot.             */
    uint32_t last_violation_at;/**< Tick of the most recent violation.       */
    uint8_t  action;           /**< sai_budget_action_t.                     */
    uint8_t  floor_prio;       /**< Demotion target priority.                */
    uint8_t  base_prio_backup; /**< Priority before a demotion (restore).    */
    uint8_t  flags;
} sai_budget_t;

/** Per-thread snapshot (sai_budget_stats()). */
typedef struct {
    uint32_t used_ticks;
    uint32_t window_ticks;
    uint32_t allowance_ticks;
    uint32_t violations;
    uint32_t last_violation_at;
    uint8_t  action;
    bool     demoted;
} sai_budget_stats_t;

/**
 * Set a thread's budget. Called from any thread; takes effect from the next
 * tick. @p window_ticks == 0 disables enforcement for the thread.
 */
sai_status_t sai_budget_set(struct sai_thread *t, uint32_t window_ticks,
                            uint32_t allowance_ticks, sai_budget_action_t action,
                            uint8_t floor_prio);

/** Clear a budget (enforcement off, stats retained). */
sai_status_t sai_budget_clear(struct sai_thread *t);

/** Snapshot of a thread's budget/accounting state. */
sai_status_t sai_budget_stats(const struct sai_thread *t, sai_budget_stats_t *out);

/** Total violations across all threads (for the stats service). */
uint32_t sai_budget_violations_total(void);

/** Inject a clock source (host tests); NULL restores the kernel tick. */
typedef uint32_t (*sai_budget_clock_fn_t)(void);
void sai_budget_set_clock(sai_budget_clock_fn_t clock_fn);

/* --- kernel-internal hooks (called from the scheduler, interrupts off) --- */
/** Called from _sai_schedule_pick() when @p prev loses the CPU. */
void _sai_budget_on_switch_out(struct sai_thread *prev);
/** Called from _sai_sched_tick() for the running thread. */
void _sai_budget_on_tick(struct sai_thread *cur);

#ifdef __cplusplus
}
#endif

#endif /* SAI_BUDGET_H */
