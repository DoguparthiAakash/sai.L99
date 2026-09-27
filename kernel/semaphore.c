/**
 * @file kernel/semaphore.c
 * @brief Counting semaphore with optional bound; ISR-safe give.
 */
#include "internal.h"

sai_status_t sai_sem_init(sai_semaphore_t *s, const char *name,
                          uint32_t initial, uint32_t max)
{
    if (s == NULL) {
        return SAI_ERR_INVAL;
    }
    memset(s, 0, sizeof(*s));
    s->kobj.type = SAI_KOBJ_SEM;
    s->kobj.name = name ? name : "sem";
    s->count = (int32_t)initial;
    s->max   = (max == 0u) ? 0 : (int32_t)max;
    s->waiters = &s->wq_store;
    s->wq_store.head  = NULL;
    s->wq_store.flags = WQ_FIFO;
    return SAI_OK;
}

static sai_status_t sem_take_impl(sai_semaphore_t *s, int32_t timeout_ms)
{
    if (s == NULL || s->waiters == NULL) {
        return SAI_ERR_INVAL;
    }
    if (port_in_isr()) {
        return SAI_ERR_PERM;
    }
    uint32_t key = port_lock();

    if (s->count > 0) {
        s->count--;
        port_unlock(key);
        return SAI_OK;
    }
    if (timeout_ms == SAI_NO_WAIT) {
        port_unlock(key);
        return SAI_ERR_WOULD_BLOCK;
    }

    sai_status_t rc = _sai_wq_block(s->waiters, 0u, timeout_ms);
    return rc;
}

sai_status_t sai_sem_take(sai_semaphore_t *s)
{
    return sem_take_impl(s, SAI_WAIT_FOREVER);
}

sai_status_t sai_sem_take_timeout(sai_semaphore_t *s, int32_t timeout_ms)
{
    return sem_take_impl(s, timeout_ms);
}

sai_status_t sai_sem_trytake(sai_semaphore_t *s)
{
    return sem_take_impl(s, SAI_NO_WAIT);
}

static sai_status_t sem_give_impl(sai_semaphore_t *s)
{
    uint32_t key = port_lock();

    sai_thread_t *w = s->waiters->head;
    if (w != NULL) {
        (void)_sai_wq_remove(w);
        if (w->wq_flags & SAI_WQF_TIMED) {
            _sai_deadline_remove(w);
            w->wq_flags &= (uint8_t)~SAI_WQF_TIMED;
        }
        w->wq_result = (uint32_t)SAI_OK;
        w->state = SAI_THREAD_READY;
        _sai_ready_insert(w);
        port_unlock(key);
        if (w->prio < (_sai_current ? _sai_current->prio : 255u)) {
            _sai_schedule();
        }
        return SAI_OK;
    }

    if (s->max > 0 && s->count >= s->max) {
        port_unlock(key);
        return SAI_ERR_FULL;
    }
    s->count++;
    port_unlock(key);
    return SAI_OK;
}

sai_status_t sai_sem_give(sai_semaphore_t *s)
{
    if (s == NULL || s->waiters == NULL) {
        return SAI_ERR_INVAL;
    }
    return sem_give_impl(s);
}

sai_status_t sai_isr_sem_give(sai_semaphore_t *s)
{
    if (s == NULL || s->waiters == NULL) {
        return SAI_ERR_INVAL;
    }
    uint32_t key = port_lock();

    sai_thread_t *w = s->waiters->head;
    if (w != NULL) {
        (void)_sai_wq_remove(w);
        if (w->wq_flags & SAI_WQF_TIMED) {
            _sai_deadline_remove(w);
            w->wq_flags &= (uint8_t)~SAI_WQF_TIMED;
        }
        w->wq_result = (uint32_t)SAI_OK;
        w->state = SAI_THREAD_READY;
        _sai_ready_insert(w);
        _sai_isr_reschedules++;
        port_unlock(key);
        return SAI_OK;
    }
    if (s->max > 0 && s->count >= s->max) {
        port_unlock(key);
        return SAI_ERR_FULL;
    }
    s->count++;
    port_unlock(key);
    return SAI_OK;
}

int32_t sai_sem_count(const sai_semaphore_t *s)
{
    return s ? s->count : 0;
}

sai_status_t sai_sem_destroy(sai_semaphore_t *s)
{
    if (s == NULL) {
        return SAI_ERR_INVAL;
    }
    if (_sai_wq_count(s->waiters) != 0u) {
        return SAI_ERR_BUSY;
    }
    s->kobj.type = SAI_KOBJ_NONE;
    return SAI_OK;
}
