/**
 * @file kernel/budget.c
 * @brief Latency-budget enforcement: accounts per-thread CPU time and fires
 *        the configured action on overrun.
 *
 * Design constraints:
 *  - Runs on the scheduler path with interrupts disabled: no blocking, no
 *    allocation, no logging through the blocking console path (violations
 *    are recorded; the shell/stats service reports them).
 *  - The clock is injectable so host tests are deterministic (fake clock).
 *  - Window model: used_ticks accumulates only while the thread RUNS; a new
 *    window starts when the clock has advanced a full window since the
 *    previous window start. This is a token-bucket with period = window.
 */
#include "internal.h"
#include <sai/budget.h>
#include <sai/trace.h>
#include <sai/kernel.h>
#include <string.h>

#if CONFIG_SAI_BUDGET

static sai_budget_clock_fn_t s_clock;          /* NULL = kernel tick      */

static uint32_t budget_now(void)
{
    return (s_clock != NULL) ? s_clock() : sai_tick_count();
}

/* The per-thread budget struct lives in the TCB (guarded by the flag). */
sai_budget_t *budget_of(struct sai_thread *t)
{
    if (t == NULL || !(t->budget_flags & SAI_TCB_BUDGET)) {
        return NULL;
    }
    return &t->budget;
}

void sai_budget_set_clock(sai_budget_clock_fn_t clock_fn)
{
    s_clock = clock_fn;
}

sai_status_t sai_budget_set(struct sai_thread *t, uint32_t window_ticks,
                            uint32_t allowance_ticks, sai_budget_action_t action,
                            uint8_t floor_prio)
{
    if (t == NULL || action > SAI_BUDGET_ACTION_KILL) {
        return SAI_ERR_INVAL;
    }
    if (window_ticks == 0u || allowance_ticks == 0u ||
        allowance_ticks > window_ticks) {
        return SAI_ERR_INVAL;
    }
    sai_budget_t *b = &t->budget;
    b->window_ticks    = window_ticks;
    b->allowance_ticks = allowance_ticks;
    b->action          = (uint8_t)action;
    b->floor_prio      = floor_prio;
    b->window_start    = budget_now();
    t->budget_flags |= SAI_TCB_BUDGET;
    return SAI_OK;
}

sai_status_t sai_budget_clear(struct sai_thread *t)
{
    if (t == NULL) {
        return SAI_ERR_INVAL;
    }
    t->budget_flags &= (uint8_t)~SAI_TCB_BUDGET;
    /* Restore a demoted thread's priority on clear. */
    if ((t->budget.flags & 0x01u) != 0u && t->state != SAI_THREAD_DEAD) {
        (void)sai_thread_set_priority(t, t->budget.base_prio_backup);
        t->budget.flags &= (uint8_t)~0x01u;
    }
    return SAI_OK;
}

sai_status_t sai_budget_stats(const struct sai_thread *t, sai_budget_stats_t *out)
{
    sai_budget_t *b = budget_of((struct sai_thread *)t);
    if (t == NULL || out == NULL) {
        return SAI_ERR_INVAL;
    }
    if (b == NULL) {
        memset(out, 0, sizeof(*out));
        return SAI_OK;
    }
    out->used_ticks       = b->used_ticks;
    out->window_ticks     = b->window_ticks;
    out->allowance_ticks  = b->allowance_ticks;
    out->violations       = b->violations;
    out->last_violation_at = b->last_violation_at;
    out->action           = b->action;
    out->demoted          = (b->flags & 0x01u) != 0u;
    return SAI_OK;
}

uint32_t sai_budget_violations_total(void)
{
    uint32_t total = 0;
    uint32_t key = port_lock();
    for (uint32_t prio = 0; prio < SAI_NUM_PRIORITIES; prio++) {
        for (sai_thread_t *t = _sai_ready[prio]; t != NULL; t = t->prio_next) {
            sai_budget_t *b = budget_of(t);
            if (b != NULL) {
                total += b->violations;
            }
        }
    }
    /* The running thread is not in any ring. */
    if (_sai_current != NULL) {
        sai_budget_t *b = budget_of(_sai_current);
        if (b != NULL) {
            total += b->violations;
        }
    }
    port_unlock(key);
    return total;
}

/** Execute the configured action for an overrun. Interrupts are off. */
static void budget_violate(struct sai_thread *t, sai_budget_t *b)
{
    b->violations++;
    b->last_violation_at = budget_now();
    b->used_ticks = 0;                 /* window restarts after action */
    b->window_start = b->last_violation_at;

    switch (b->action) {
    case SAI_BUDGET_ACTION_DEMOTE:
        if ((b->flags & 0x01u) == 0u) {
            b->base_prio_backup = t->base_prio;
            b->flags |= 0x01u;
        }
        if (t->base_prio > b->floor_prio) {   /* only demote, never promote */
            (void)sai_thread_set_priority(t, b->floor_prio);
        }
        break;
    case SAI_BUDGET_ACTION_KILL:
        t->flags |= SAI_THREAD_FLAG_DYING;
        break;
    case SAI_BUDGET_ACTION_LOG:
    default:
        break;                       /* recorded in b->violations */
    }

    /* Trace + deferred logging (safe from ISR context). */
    _sai_trace_event(SAI_TRACE_BUDGET, (uint32_t)(uintptr_t)t,
                     b->violations, b->action);
    SAI_LOGW("budget", "thread '%s' over budget (win=%u allow=%u used) act=%u",
             t->kobj.name ? t->kobj.name : "?",
             (unsigned)b->window_ticks, (unsigned)b->allowance_ticks,
             (unsigned)b->action);
}

void _sai_budget_on_switch_out(struct sai_thread *prev)
{
    sai_budget_t *b = budget_of(prev);
    if (b == NULL) {
        return;
    }
    uint32_t now = budget_now();
    uint32_t ran = now - prev->budget_run_start;
    prev->budget_run_start = now;

    if (now - b->window_start >= b->window_ticks) {
        /* Window rolled over while running: new window keeps this slice. */
        b->window_start = now;
        b->used_ticks = 0u;
    }
    b->used_ticks += ran;
    if (b->used_ticks > b->allowance_ticks) {
        budget_violate(prev, b);
    }
}

void _sai_budget_on_tick(struct sai_thread *cur)
{
    sai_budget_t *b = budget_of(cur);
    if (b == NULL) {
        return;
    }
    b->used_ticks++;
    uint32_t now = budget_now();
    if (now - b->window_start >= b->window_ticks) {
        /* Full window elapsed: reset the bucket. */
        b->window_start = now;
        b->used_ticks = 0u;
        return;
    }
    if (b->used_ticks > b->allowance_ticks) {
        budget_violate(cur, b);
    }
}

#else  /* !CONFIG_SAI_BUDGET: stubs keep call sites link-clean */

void _sai_budget_on_switch_out(struct sai_thread *prev) { (void)prev; }
void _sai_budget_on_tick(struct sai_thread *cur)        { (void)cur; }
uint32_t sai_budget_violations_total(void)              { return 0u; }
sai_status_t sai_budget_set(struct sai_thread *t, uint32_t w, uint32_t a,
                            sai_budget_action_t act, uint8_t f)
{ (void)t; (void)w; (void)a; (void)act; (void)f; return SAI_ERR_NOTSUP; }
sai_status_t sai_budget_clear(struct sai_thread *t)     { (void)t; return SAI_ERR_NOTSUP; }
sai_status_t sai_budget_stats(const struct sai_thread *t, sai_budget_stats_t *o)
{ (void)t; (void)o; return SAI_ERR_NOTSUP; }
void sai_budget_set_clock(sai_budget_clock_fn_t clock_fn) { (void)clock_fn; }

#endif /* CONFIG_SAI_BUDGET */
