/**
 * @file kernel/time.c
 * @brief Kernel time: tick counter, uptime, busy-wait, timer objects.
 *
 * Timers are kept on a single sorted-by-deadline list. _sai_timers_tick()
 * is called from the tick path; _sai_timers_next_deadline() supports
 * tickless idle by reporting the next expiry.
 */
#include "internal.h"
#include <sai/time.h>

volatile uint32_t _sai_tick_count = 0;

uint32_t sai_tick_count(void)
{
    return _sai_tick_count;
}

uint32_t sai_uptime_ms(void)
{
    return _sai_tick_count * (uint32_t)SAI_TICK_MS;
}

void sai_busy_sleep_us(uint32_t us)
{
    /* Portable fallback: calibrated against SAI_TICK_MS. Ports may override
     * with a cycle-counter delay loop. */
    volatile uint32_t loops = us * (uint32_t)(1000u / SAI_TICK_MS) + 1u;
    while (loops--) {
        /* spin */
    }
}

/* ------------------------------------------------------------------ */
/* Tick entry                                                          */
/* ------------------------------------------------------------------ */
void _sai_tick_handler(void)
{
    uint32_t next_deadline = _sai_next_deadline_ticks();
    if (next_deadline != 0u &&
        (int32_t)(next_deadline - _sai_tick_count) > 1) {
        /* Fast path: no deadline within one tick and no active timers
         * (checked below) — just advance the count.  The tickless idle
         * programming layer skips most of these ticks entirely on target. */
        if (_sai_timers_next_deadline() == 0u &&
            _sai_current != NULL && _sai_current->prio == SAI_IDLE_PRIORITY) {
            _sai_tick_count++;
            return;
        }
    }

    _sai_tick_count++;

    _sai_deadlines_tick();     /* wake sleepers / timed waiters */
    _sai_timers_tick();        /* run expired timer callbacks   */
    _sai_sched_tick();         /* timeslice accounting          */
}

/* ------------------------------------------------------------------ */
/* Timer list                                                          */
/* ------------------------------------------------------------------ */
static sai_timer_t *s_timers = NULL;

sai_status_t sai_timer_init(sai_timer_t *t, const char *name,
                            sai_timer_fn_t fn, void *arg,
                            uint32_t interval_ms, bool periodic)
{
    if (t == NULL || fn == NULL || interval_ms == 0) {
        return SAI_ERR_INVAL;
    }
    memset(t, 0, sizeof(*t));
    t->kobj.type = SAI_KOBJ_TIMER;
    t->kobj.name = name ? name : "timer";
    t->fn        = fn;
    t->arg       = arg;
    t->interval  = SAI_MS_TO_TICKS(interval_ms);
    if (t->interval == 0) {
        t->interval = 1;
    }
    t->periodic  = periodic ? 1u : 0u;
    return SAI_OK;
}

sai_status_t sai_timer_start(sai_timer_t *t)
{
    if (t == NULL || t->fn == NULL) {
        return SAI_ERR_INVAL;
    }
    uint32_t key = port_lock();

    if (t->active) {
        /* unlink */
        sai_timer_t **pp = &s_timers;
        while (*pp != NULL && *pp != t) {
            pp = &(*pp)->next;
        }
        if (*pp == t) {
            *pp = t->next;
        }
    }
    t->deadline = _sai_tick_count + t->interval;
    t->active   = 1u;

    /* sorted insert by deadline */
    sai_timer_t **pp = &s_timers;
    while (*pp != NULL && (int32_t)((*pp)->deadline - t->deadline) <= 0) {
        pp = &(*pp)->next;
    }
    t->next = *pp;
    *pp = t;

    port_unlock(key);
    return SAI_OK;
}

sai_status_t sai_timer_stop(sai_timer_t *t)
{
    if (t == NULL) {
        return SAI_ERR_INVAL;
    }
    uint32_t key = port_lock();
    sai_timer_t **pp = &s_timers;
    while (*pp != NULL && *pp != t) {
        pp = &(*pp)->next;
    }
    if (*pp == t) {
        *pp = t->next;
        t->next   = NULL;
        t->active = 0u;
    }
    port_unlock(key);
    return SAI_OK;
}

sai_status_t sai_timer_restart(sai_timer_t *t, uint32_t interval_ms)
{
    if (t == NULL || interval_ms == 0) {
        return SAI_ERR_INVAL;
    }
    t->interval = SAI_MS_TO_TICKS(interval_ms);
    if (t->interval == 0) {
        t->interval = 1;
    }
    return sai_timer_start(t);
}

bool sai_timer_is_active(const sai_timer_t *t)
{
    return t != NULL && t->active != 0u;
}

uint32_t sai_timer_remaining_ms(const sai_timer_t *t)
{
    if (t == NULL || !t->active) {
        return 0;
    }
    uint32_t now = _sai_tick_count;
    uint32_t remaining_ticks =
        (int32_t)(t->deadline - now) > 0 ? (t->deadline - now) : 0u;
    return remaining_ticks * (uint32_t)SAI_TICK_MS;
}

uint32_t sai_timer_active_count(void)
{
    uint32_t key = port_lock();
    uint32_t n = 0;
    for (sai_timer_t *t = s_timers; t != NULL; t = t->next) {
        n++;
    }
    port_unlock(key);
    return n;
}

void _sai_timers_tick(void)
{
    uint32_t now = _sai_tick_count;

    /* Expire all due timers. Callbacks run in tick context (interrupts are
     * disabled at this point on target; the callback contract forbids
     * blocking). Collect them first, then fire, so callbacks may restart
     * or stop timers safely. */
    sai_timer_t *due = NULL;
    sai_timer_t **tail = &due;

    uint32_t key = port_lock();
    while (s_timers != NULL && (int32_t)(s_timers->deadline - now) <= 0) {
        sai_timer_t *t = s_timers;
        s_timers = t->next;
        t->next = NULL;
        t->active = t->periodic;
        *tail = t;
        tail = &t->next;
    }
    port_unlock(key);

    while (due != NULL) {
        sai_timer_t *t = due;
        due = due->next;
        t->next = NULL;
        t->fn(t->arg);
        if (t->periodic && t->active) {
            /* re-arm periodic timers (deadline computed from now) */
            t->deadline = _sai_tick_count + t->interval;
            t->active = 1u;
            uint32_t k2 = port_lock();
            sai_timer_t **pp = &s_timers;
            while (*pp != NULL && (int32_t)((*pp)->deadline - t->deadline) <= 0) {
                pp = &(*pp)->next;
            }
            t->next = *pp;
            *pp = t;
            port_unlock(k2);
        }
    }
}

uint32_t _sai_timers_next_deadline(void)
{
    uint32_t key = port_lock();
    uint32_t d = (s_timers != NULL) ? s_timers->deadline : 0u;
    port_unlock(key);
    return d;
}
