/**
 * @file kernel/mutex.c
 * @brief Mutex with priority inheritance and deadlock detection.
 *
 * PI protocol: when a thread blocks on a mutex it donates its priority to
 * the owner via the owner's held-mutex chain. On release, priorities are
 * recomputed from remaining owned mutexes.
 */
#include "internal.h"

/* ------------------------------------------------------------------ */
/* Priority inheritance                                                */
/* ------------------------------------------------------------------ */
static void pi_recompute(sai_thread_t *t)
{
    uint8_t effective = t->base_prio;
    for (sai_mutex_t *m = t->held_mutexes; m != NULL; m = m->next_owned) {
        for (sai_thread_t *w = m->waiters->head; w != NULL; w = w->wq_next) {
            if (w->base_prio < effective) {
                effective = w->base_prio;
            }
        }
    }
    if (effective != t->prio) {
        bool was_ready = (t->state == SAI_THREAD_READY);
        if (was_ready) {
            _sai_ready_remove(t);          /* re-bin into new priority ring */
        }
        t->prio = effective;
        if (was_ready) {
            t->state = SAI_THREAD_READY;
            _sai_ready_insert(t);
        }
        if (t == _sai_current && _sai_current &&
            _sai_current->prio < t->prio) {
            ; /* nothing: current thread's prio lowered */
        }
    }
}

void _sai_mutex_pi_block(sai_mutex_t *m, sai_thread_t *waiter)
{
    if (m->owner != NULL && waiter->base_prio < m->owner->prio) {
        bool was_ready = (m->owner->state == SAI_THREAD_READY);
        if (was_ready) {
            _sai_ready_remove(m->owner);
        }
        m->owner->prio = waiter->base_prio;
        if (was_ready) {
            m->owner->state = SAI_THREAD_READY;
            _sai_ready_insert(m->owner);
        }
    }
}

void _sai_mutex_pi_unblock(sai_mutex_t *m, sai_thread_t *waiter)
{
    (void)m;
    (void)waiter;
    /* Owner's effective priority is recomputed on release (pi_recompute). */
}

/* ------------------------------------------------------------------ */
/* Mutex API                                                           */
/* ------------------------------------------------------------------ */
sai_status_t sai_mutex_init(sai_mutex_t *m, const char *name, bool recursive)
{
    if (m == NULL) {
        return SAI_ERR_INVAL;
    }
    memset(m, 0, sizeof(*m));
    m->kobj.type  = SAI_KOBJ_MUTEX;
    m->kobj.name  = name ? name : "mutex";
    m->recursive  = recursive ? 1u : 0u;
    m->waiters    = &m->wq_store;
    m->wq_store.head = NULL;
    m->wq_store.flags = WQ_LOCK;           /* sorted by priority */
    return SAI_OK;
}

sai_status_t sai_mutex_lock_timeout(sai_mutex_t *m, int32_t timeout_ms)
{
    if (m == NULL || m->waiters == NULL) {
        return SAI_ERR_INVAL;
    }
    if (port_in_isr()) {
        return SAI_ERR_PERM;
    }
    sai_thread_t *self = _sai_current;

    uint32_t key = port_lock();

    if (m->owner == NULL) {
        m->owner = self;
        m->lock_count = 1;
        /* push onto the owner's held chain */
        m->next_owned = self->held_mutexes;
        self->held_mutexes = m;
        port_unlock(key);
        return SAI_OK;
    }

    if (m->owner == self) {
        if (m->recursive) {
            m->lock_count++;
            port_unlock(key);
            return SAI_OK;
        }
        port_unlock(key);
        return SAI_ERR_DEADLOCK;
    }

    if (timeout_ms == SAI_NO_WAIT) {
        port_unlock(key);
        return SAI_ERR_WOULD_BLOCK;
    }

    /* Donate priority to the owner before enqueueing. */
    _sai_mutex_pi_block(m, self);

    sai_status_t rc = _sai_wq_block(m->waiters, self->base_prio, timeout_ms);
    /* wq_block released the lock and blocked us; we resume here when woken. */
    if (rc == SAI_OK) {
        uint32_t k2 = port_lock();
        m->owner = self;
        m->lock_count = 1;
        m->next_owned = self->held_mutexes;
        self->held_mutexes = m;
        port_unlock(k2);
    } else {
        /* timed out: undo the donation */
        uint32_t k2 = port_lock();
        pi_recompute(m->owner);
        port_unlock(k2);
    }
    return rc;
}

sai_status_t sai_mutex_lock(sai_mutex_t *m)
{
    return sai_mutex_lock_timeout(m, SAI_WAIT_FOREVER);
}

sai_status_t sai_mutex_trylock(sai_mutex_t *m)
{
    return sai_mutex_lock_timeout(m, SAI_NO_WAIT);
}

sai_status_t sai_mutex_unlock(sai_mutex_t *m)
{
    if (m == NULL || m->waiters == NULL) {
        return SAI_ERR_INVAL;
    }
    if (port_in_isr()) {
        return SAI_ERR_PERM;
    }
    uint32_t key = port_lock();

    if (m->owner != _sai_current) {
        port_unlock(key);
        return SAI_ERR_PERM;
    }

    if (m->recursive && m->lock_count > 1) {
        m->lock_count--;
        port_unlock(key);
        return SAI_OK;
    }

    /* unlink from owner's held chain */
    sai_thread_t *self = _sai_current;
    sai_mutex_t **pp = &self->held_mutexes;
    while (*pp != NULL && *pp != m) {
        pp = &(*pp)->next_owned;
    }
    if (*pp == m) {
        *pp = m->next_owned;
    }
    m->next_owned = NULL;
    m->owner = NULL;
    m->lock_count = 0;

    /* hand off to the highest-priority waiter (queue is sorted) */
    sai_thread_t *w = m->waiters->head;
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

    /* restore our own effective priority */
    pi_recompute(self);
    port_unlock(key);

    if (w != NULL && w->prio < (_sai_current ? _sai_current->prio : 255u)) {
        _sai_schedule();
    }
    return SAI_OK;
}

sai_thread_t *sai_mutex_owner(const sai_mutex_t *m)
{
    return m ? m->owner : NULL;
}

sai_status_t sai_mutex_destroy(sai_mutex_t *m)
{
    if (m == NULL) {
        return SAI_ERR_INVAL;
    }
    if (m->owner != NULL || _sai_wq_count(m->waiters) != 0u) {
        return SAI_ERR_BUSY;
    }
    m->kobj.type = SAI_KOBJ_NONE;
    return SAI_OK;
}

