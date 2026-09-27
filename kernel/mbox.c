/**
 * @file kernel/mbox.c
 * @brief Byte mailbox: single-byte FIFO with blocking/timeout/ISR put.
 */
#include "internal.h"
#include <sai/mem.h>

sai_status_t sai_mbox_init(sai_mbox_t *b, const char *name, void *buffer, uint32_t size)
{
    if (b == NULL || size == 0) {
        return SAI_ERR_INVAL;
    }
    memset(b, 0, sizeof(*b));
    b->kobj.type = SAI_KOBJ_MBOX;
    b->kobj.name = name ? name : "mbox";

    if (buffer == NULL) {
        b->buf = sai_malloc(size);
        if (b->buf == NULL) {
            return SAI_ERR_NOMEM;
        }
        b->alloc = true;
    } else {
        b->buf = buffer;
        b->alloc = false;
    }
    b->size = size;
    b->put_wait = &b->put_store;
    b->get_wait = &b->get_store;
    b->put_store.head = NULL; b->put_store.flags = WQ_FIFO;
    b->get_store.head = NULL; b->get_store.flags = WQ_FIFO;
    return SAI_OK;
}

sai_status_t sai_mbox_put(sai_mbox_t *b, uint8_t byte, int32_t timeout_ms)
{
    if (b == NULL || b->put_wait == NULL) {
        return SAI_ERR_INVAL;
    }
    if (port_in_isr()) {
        return sai_isr_mbox_put(b, byte);
    }
    for (;;) {
        uint32_t key = port_lock();
        if (b->count < b->size) {
            b->buf[(b->head + b->count) % b->size] = byte;
            b->count++;
            port_unlock(key);

            sai_thread_t *w = b->get_wait->head;
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
        sai_status_t rc = _sai_wq_block(b->put_wait, 0u, timeout_ms);
        if (rc != SAI_OK) {
            return rc;
        }
    }
}

sai_status_t sai_mbox_get(sai_mbox_t *b, uint8_t *byte, int32_t timeout_ms)
{
    if (b == NULL || byte == NULL || b->get_wait == NULL) {
        return SAI_ERR_INVAL;
    }
    for (;;) {
        uint32_t key = port_lock();
        if (b->count > 0) {
            *byte = b->buf[b->head];
            b->head = (b->head + 1u) % b->size;
            b->count--;
            port_unlock(key);

            sai_thread_t *w = b->put_wait->head;
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
        sai_status_t rc = _sai_wq_block(b->get_wait, 0u, timeout_ms);
        if (rc != SAI_OK) {
            return rc;
        }
    }
}

sai_status_t sai_isr_mbox_put(struct sai_mbox *b, uint8_t byte)
{
    if (b == NULL || b->buf == NULL) {
        return SAI_ERR_INVAL;
    }
    uint32_t key = port_lock();
    if (b->count == b->size) {
        port_unlock(key);
        return SAI_ERR_FULL;
    }
    b->buf[(b->head + b->count) % b->size] = byte;
    b->count++;
    port_unlock(key);

    sai_thread_t *w = b->get_wait->head;
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

uint32_t sai_mbox_count(const sai_mbox_t *b)
{
    return b ? b->count : 0u;
}

sai_status_t sai_mbox_destroy(sai_mbox_t *b)
{
    if (b == NULL) {
        return SAI_ERR_INVAL;
    }
    if (_sai_wq_count(b->put_wait) || _sai_wq_count(b->get_wait)) {
        return SAI_ERR_BUSY;
    }
    if (b->alloc) {
        sai_free(b->buf);
        b->buf = NULL;
    }
    b->kobj.type = SAI_KOBJ_NONE;
    return SAI_OK;
}
