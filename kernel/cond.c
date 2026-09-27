/**
 * @file kernel/cond.c
 * @brief Condition variables (Mesa style, bound to a sai_mutex_t).
 */
#include "internal.h"

sai_status_t sai_cond_init(sai_cond_t *c, const char *name)
{
    if (c == NULL) {
        return SAI_ERR_INVAL;
    }
    memset(c, 0, sizeof(*c));
    c->kobj.type = SAI_KOBJ_COND;
    c->kobj.name = name ? name : "cond";
    c->waiters = &c->wq_store;
    c->wq_store.head  = NULL;
    c->wq_store.flags = WQ_PRIO;      /* wake highest-priority waiter first */
    return SAI_OK;
}

sai_status_t sai_cond_wait_timeout(sai_cond_t *c, sai_mutex_t *m, int32_t timeout_ms)
{
    if (c == NULL || m == NULL || c->waiters == NULL) {
        return SAI_ERR_INVAL;
    }
    if (m->owner != _sai_current) {
        return SAI_ERR_PERM;          /* must hold the mutex to wait */
    }

    uint8_t saved_base = _sai_current->base_prio;
    uint8_t saved_eff  = _sai_current->prio;

    /* Release the mutex fully (even recursive holds: wait releases all). */
    uint32_t count = m->lock_count;
    for (uint32_t i = 0; i < count; i++) {
        (void)sai_mutex_unlock(m);
    }

    /* Wait; re-acquire all counts before returning. */
    sai_status_t rc = _sai_wq_block(c->waiters, saved_base, timeout_ms);

    uint32_t reacquired = 0;
    while (reacquired < count) {
        sai_status_t lk = sai_mutex_lock(m);
        if (lk != SAI_OK) {
            return (lk == SAI_ERR_DEADLOCK) ? lk : lk;
        }
        reacquired++;
    }

    /* Restore priority in case PI changed it while we held nothing. */
    if (_sai_current->prio != saved_eff && _sai_current->base_prio == saved_base) {
        uint32_t key = port_lock();
        bool was_ready = (_sai_current->state == SAI_THREAD_READY);
        if (was_ready) {
            _sai_ready_remove(_sai_current);
        }
        _sai_current->prio = saved_eff;
        if (was_ready) {
            _sai_current->state = SAI_THREAD_READY;
            _sai_ready_insert(_sai_current);
        }
        port_unlock(key);
    }
    return rc;
}

sai_status_t sai_cond_wait(sai_cond_t *c, sai_mutex_t *m)
{
    return sai_cond_wait_timeout(c, m, SAI_WAIT_FOREVER);
}

sai_status_t sai_cond_signal(sai_cond_t *c)
{
    if (c == NULL || c->waiters == NULL) {
        return SAI_ERR_INVAL;
    }
    uint32_t key = port_lock();
    sai_thread_t *w = c->waiters->head;
    if (w != NULL) {
        (void)_sai_wq_remove(w);
        if (w->wq_flags & SAI_WQF_TIMED) {
            _sai_deadline_remove(w);
            w->wq_flags &= (uint8_t)~SAI_WQF_TIMED;
        }
        w->wq_result = (uint32_t)SAI_OK;
        w->state = SAI_THREAD_READY;
        _sai_ready_insert(w);
    }
    port_unlock(key);
    if (w != NULL && w->prio < (_sai_current ? _sai_current->prio : 255u)) {
        _sai_schedule();
    }
    return SAI_OK;
}

sai_status_t sai_cond_broadcast(sai_cond_t *c)
{
    if (c == NULL || c->waiters == NULL) {
        return SAI_ERR_INVAL;
    }
    uint32_t key = port_lock();
    uint32_t woke = 0;
    for (;;) {
        sai_thread_t *w = c->waiters->head;
        if (w == NULL) {
            break;
        }
        (void)_sai_wq_remove(w);
        if (w->wq_flags & SAI_WQF_TIMED) {
            _sai_deadline_remove(w);
            w->wq_flags &= (uint8_t)~SAI_WQF_TIMED;
        }
        w->wq_result = (uint32_t)SAI_OK;
        w->state = SAI_THREAD_READY;
        _sai_ready_insert(w);
        woke++;
    }
    port_unlock(key);
    if (woke > 0) {
        _sai_schedule();
    }
    return SAI_OK;
}

sai_status_t sai_cond_destroy(sai_cond_t *c)
{
    if (c == NULL) {
        return SAI_ERR_INVAL;
    }
    if (_sai_wq_count(c->waiters) != 0u) {
        return SAI_ERR_BUSY;
    }
    c->kobj.type = SAI_KOBJ_NONE;
    return SAI_OK;
}
