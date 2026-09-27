/**
 * @file kernel/msgq.c
 * @brief Fixed-size message queue with blocking, timeout and overwrite modes.
 */
#include "internal.h"
#include <sai/mem.h>

sai_status_t sai_msgq_init(sai_msgq_t *q, const char *name, void *buffer,
                           uint32_t msg_size, uint32_t max_msgs)
{
    if (q == NULL || msg_size == 0 || max_msgs == 0) {
        return SAI_ERR_INVAL;
    }
    memset(q, 0, sizeof(*q));
    q->kobj.type = SAI_KOBJ_MSGQ;
    q->kobj.name = name ? name : "msgq";
    q->msg_size  = msg_size;
    q->max_msgs  = max_msgs;

    if (buffer == NULL) {
        q->buf = sai_malloc(msg_size * max_msgs);
        if (q->buf == NULL) {
            return SAI_ERR_NOMEM;
        }
        q->alloc = true;
    } else {
        q->buf = buffer;
        q->alloc = false;
    }

    q->put_wait = &q->put_store;
    q->get_wait = &q->get_store;
    q->put_store.head = NULL; q->put_store.flags = WQ_FIFO;
    q->get_store.head = NULL; q->get_store.flags = WQ_FIFO;
    return SAI_OK;
}

static uint8_t *msg_slot(const sai_msgq_t *q, uint32_t idx)
{
    return q->buf + idx * q->msg_size;
}

sai_status_t sai_msgq_put(sai_msgq_t *q, const void *msg, int32_t timeout_ms)
{
    if (q == NULL || msg == NULL) {
        return SAI_ERR_INVAL;
    }
    if (port_in_isr()) {
        return sai_isr_msgq_put(q, msg);
    }
    for (;;) {
        uint32_t key = port_lock();
        if (q->count < q->max_msgs) {
            uint8_t *slot = msg_slot(q, (q->head + q->count) % q->max_msgs);
            memcpy(slot, msg, q->msg_size);
            q->count++;
            port_unlock(key);

            /* wake one getter */
            sai_thread_t *w = q->get_wait->head;
            if (w != NULL) {
                (void)_sai_wq_remove(w);
                if (w->wq_flags & SAI_WQF_TIMED) {
                    _sai_deadline_remove(w);
                    w->wq_flags &= (uint8_t)~SAI_WQF_TIMED;
                }
                w->wq_result = (uint32_t)SAI_OK;
                w->state = SAI_THREAD_READY;
                _sai_ready_insert(w);
                if (w->prio < (_sai_current ? _sai_current->prio : 255u)) {
                    _sai_schedule();
                }
            }
            return SAI_OK;
        }
        port_unlock(key);

        if (timeout_ms == SAI_NO_WAIT) {
            return SAI_ERR_FULL;
        }
        sai_status_t rc = _sai_wq_block(q->put_wait, 0u, timeout_ms);
        if (rc != SAI_OK) {
            return rc;                    /* timeout */
        }
        /* woken: loop and retry the put */
    }
}

sai_status_t sai_msgq_get(sai_msgq_t *q, void *msg, int32_t timeout_ms)
{
    if (q == NULL || msg == NULL) {
        return SAI_ERR_INVAL;
    }
    if (port_in_isr()) {
        return SAI_ERR_PERM;
    }
    for (;;) {
        uint32_t key = port_lock();
        if (q->count > 0) {
            memcpy(msg, msg_slot(q, q->head), q->msg_size);
            q->head = (q->head + 1u) % q->max_msgs;
            q->count--;
            port_unlock(key);

            /* wake one putter */
            sai_thread_t *w = q->put_wait->head;
            if (w != NULL) {
                (void)_sai_wq_remove(w);
                if (w->wq_flags & SAI_WQF_TIMED) {
                    _sai_deadline_remove(w);
                    w->wq_flags &= (uint8_t)~SAI_WQF_TIMED;
                }
                w->wq_result = (uint32_t)SAI_OK;
                w->state = SAI_THREAD_READY;
                _sai_ready_insert(w);
                if (w->prio < (_sai_current ? _sai_current->prio : 255u)) {
                    _sai_schedule();
                }
            }
            return SAI_OK;
        }
        port_unlock(key);

        if (timeout_ms == SAI_NO_WAIT) {
            return SAI_ERR_EMPTY;
        }
        sai_status_t rc = _sai_wq_block(q->get_wait, 0u, timeout_ms);
        if (rc != SAI_OK) {
            return rc;
        }
    }
}

sai_status_t sai_msgq_put_overwrite(sai_msgq_t *q, const void *msg)
{
    if (q == NULL || msg == NULL) {
        return SAI_ERR_INVAL;
    }
    uint32_t key = port_lock();
    if (q->count == q->max_msgs) {
        /* drop oldest */
        q->head = (q->head + 1u) % q->max_msgs;
        q->count--;
    }
    uint8_t *slot = msg_slot(q, (q->head + q->count) % q->max_msgs);
    memcpy(slot, msg, q->msg_size);
    q->count++;
    port_unlock(key);

    sai_thread_t *w = q->get_wait->head;
    if (w != NULL) {
        (void)_sai_wq_remove(w);
        if (w->wq_flags & SAI_WQF_TIMED) {
            _sai_deadline_remove(w);
            w->wq_flags &= (uint8_t)~SAI_WQF_TIMED;
        }
        w->wq_result = (uint32_t)SAI_OK;
        w->state = SAI_THREAD_READY;
        _sai_ready_insert(w);
        if (w->prio < (_sai_current ? _sai_current->prio : 255u)) {
            _sai_schedule();
        }
    }
    return SAI_OK;
}

sai_status_t sai_isr_msgq_put(struct sai_msgq *q, const void *msg)
{
    if (q == NULL || msg == NULL) {
        return SAI_ERR_INVAL;
    }
    uint32_t key = port_lock();
    if (q->count == q->max_msgs) {
        port_unlock(key);
        return SAI_ERR_FULL;         /* ISR puts never overwrite */
    }
    uint8_t *slot = msg_slot(q, (q->head + q->count) % q->max_msgs);
    memcpy(slot, msg, q->msg_size);
    q->count++;
    port_unlock(key);

    sai_thread_t *w = q->get_wait->head;
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
    }
    return SAI_OK;
}

uint32_t sai_msgq_count(const sai_msgq_t *q)
{
    return q ? q->count : 0u;
}

uint32_t sai_msgq_space(const sai_msgq_t *q)
{
    return q ? (q->max_msgs - q->count) : 0u;
}

sai_status_t sai_msgq_destroy(sai_msgq_t *q)
{
    if (q == NULL) {
        return SAI_ERR_INVAL;
    }
    if (_sai_wq_count(q->put_wait) || _sai_wq_count(q->get_wait)) {
        return SAI_ERR_BUSY;
    }
    if (q->alloc) {
        sai_free(q->buf);
        q->buf = NULL;
    }
    q->kobj.type = SAI_KOBJ_NONE;
    return SAI_OK;
}
