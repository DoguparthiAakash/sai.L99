/**
 * @file kernel/tickless.c
 * @brief Kernel-side tickless idle: next deadline computation shared by ports.
 */
#include "internal.h"

uint32_t _sai_next_deadline_ticks(void)
{
    uint32_t now = _sai_tick_count;
    uint32_t next = 0;

    /* next sleeping/timed thread */
    extern sai_thread_t *_sai_deadline_head(void);
    sai_thread_t *d = _sai_deadline_head();
    if (d != NULL) {
        uint32_t rel = d->wake_at - now;
        next = (rel == 0u) ? 1u : rel;
    }

    /* next kernel timer */
    uint32_t tnext = _sai_timers_next_deadline();
    if (tnext != 0u) {
        uint32_t rel = tnext - now;
        if (rel == 0u) {
            rel = 1u;
        }
        if (next == 0u || rel < next) {
            next = rel;
        }
    }
    return next;
}
