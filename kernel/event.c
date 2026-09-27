/**
 * @file kernel/event.c
 * @brief Event flag groups: wait-any / wait-all, optional consume-on-exit.
 */
#include "internal.h"

sai_status_t sai_event_init(sai_event_t *e, const char *name)
{
    if (e == NULL) {
        return SAI_ERR_INVAL;
    }
    memset(e, 0, sizeof(*e));
    e->kobj.type = SAI_KOBJ_EVENT;
    e->kobj.name = name ? name : "event";
    e->waiters = &e->wq_store;
    e->wq_store.head  = NULL;
    e->wq_store.flags = WQ_EVENT;      /* key = requested mask */
    return SAI_OK;
}

/* Wake every waiter whose request is satisfied; returns count woken. */
static uint32_t event_pump(sai_event_t *e)
{
    uint32_t woke = 0;
    sai_thread_t **pp = &e->waiters->head;

    while (*pp != NULL) {
        sai_thread_t *w = *pp;
        uint32_t want = w->wq_key;
        bool satisfied = (want == 0u) ||
                         ((e->flags & want) == want) ||
                         (((e->flags & want) != 0u) && (w->wq_flags & SAI_WQF_ANY) != 0u);
        if (satisfied) {
            *pp = w->wq_next;
            w->wq_next = NULL;
            w->wq = NULL;
            if (w->wq_flags & SAI_WQF_TIMED) {
                _sai_deadline_remove(w);
                w->wq_flags &= (uint8_t)~SAI_WQF_TIMED;
            }
            /* Status = SAI_OK; the delivered bits travel in wq_flags_out. */
            w->wq_result = (uint32_t)SAI_OK;
            w->wq_flags_out = e->flags & want;
            if (w->wq_flags & SAI_WQF_CONSUME) {
                e->flags &= ~want;
            }
            w->state = SAI_THREAD_READY;
            _sai_ready_insert(w);
            woke++;
            continue;                      /* pp stays: next element */
        }
        pp = &w->wq_next;
    }
    return woke;
}

sai_status_t sai_event_set(sai_event_t *e, uint32_t flags)
{
    if (e == NULL || e->waiters == NULL) {
        return SAI_ERR_INVAL;
    }
    uint32_t key = port_lock();
    e->flags |= flags;
    uint32_t woke = event_pump(e);
    port_unlock(key);

    if (woke > 0) {
        if (_sai_current != NULL) {
            _sai_schedule();
        }
    }
    return SAI_OK;
}

sai_status_t sai_isr_event_set(struct sai_event *e, uint32_t flags)
{
    if (e == NULL || e->waiters == NULL) {
        return SAI_ERR_INVAL;
    }
    uint32_t key = port_lock();
    e->flags |= flags;
    uint32_t woke = event_pump(e);
    if (woke > 0) {
        _sai_isr_reschedules++;
    }
    port_unlock(key);
    return SAI_OK;
}

sai_status_t sai_event_wait(sai_event_t *e, uint32_t flags, uint32_t opts,
                            uint32_t *set_flags, int32_t timeout_ms)
{
    if (e == NULL || e->waiters == NULL) {
        return SAI_ERR_INVAL;
    }
    if (port_in_isr()) {
        return SAI_ERR_PERM;
    }
    uint32_t key = port_lock();

    bool satisfied = (flags == 0u) ||
                     ((e->flags & flags) == flags) ||
                     (((e->flags & flags) != 0u) && (opts & SAI_EVENT_WAIT_ANY) != 0u);

    if (satisfied) {
        uint32_t got = e->flags & flags;
        if (opts & SAI_EVENT_CONSUME) {
            e->flags &= ~flags;
        }
        port_unlock(key);
        if (set_flags != NULL) {
            *set_flags = got;
        }
        return SAI_OK;
    }
    port_unlock(key);

    if (timeout_ms == SAI_NO_WAIT) {
        return SAI_ERR_WOULD_BLOCK;
    }

    uint8_t myflags = 0;
    if (opts & SAI_EVENT_WAIT_ANY) {
        myflags |= SAI_WQF_ANY;
    }
    if (opts & SAI_EVENT_CONSUME) {
        myflags |= SAI_WQF_CONSUME;
    }

    uint32_t save = _sai_current->wq_flags;
    _sai_current->wq_flags |= myflags;

    sai_status_t rc = _sai_wq_block(e->waiters, flags, timeout_ms);

    if (rc == SAI_OK && set_flags != NULL) {
        *set_flags = _sai_current->wq_flags_out;
    }
    _sai_current->wq_flags = save;
    return rc;
}

uint32_t sai_event_get(const sai_event_t *e)
{
    return e ? e->flags : 0u;
}

sai_status_t sai_event_clear(sai_event_t *e, uint32_t flags)
{
    if (e == NULL) {
        return SAI_ERR_INVAL;
    }
    uint32_t key = port_lock();
    e->flags &= ~flags;
    port_unlock(key);
    return SAI_OK;
}

sai_status_t sai_event_destroy(sai_event_t *e)
{
    if (e == NULL) {
        return SAI_ERR_INVAL;
    }
    if (_sai_wq_count(e->waiters) != 0u) {
        return SAI_ERR_BUSY;
    }
    e->kobj.type = SAI_KOBJ_NONE;
    return SAI_OK;
}
