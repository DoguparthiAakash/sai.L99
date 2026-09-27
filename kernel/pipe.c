/**
 * @file kernel/pipe.c
 * @brief Byte pipe: stream semantics with partial read/write and backpressure.
 */
#include "internal.h"
#include <sai/mem.h>

sai_status_t sai_pipe_init(sai_pipe_t *p, const char *name, void *buffer, uint32_t size)
{
    if (p == NULL || size == 0) {
        return SAI_ERR_INVAL;
    }
    memset(p, 0, sizeof(*p));
    p->kobj.type = SAI_KOBJ_PIPE;
    p->kobj.name = name ? name : "pipe";

    if (buffer == NULL) {
        p->buf = sai_malloc(size);
        if (p->buf == NULL) {
            return SAI_ERR_NOMEM;
        }
        p->alloc = true;
    } else {
        p->buf = buffer;
        p->alloc = false;
    }
    p->size = size;
    p->put_wait = &p->put_store;
    p->get_wait = &p->get_store;
    p->put_store.head = NULL; p->put_store.flags = WQ_FIFO;
    p->get_store.head = NULL; p->get_store.flags = WQ_FIFO;
    return SAI_OK;
}

/* wake helper shared by both sides */
static void pipe_wake(sai_waitqueue_t *wq)
{
    sai_thread_t *w = wq->head;
    if (w == NULL) {
        return;
    }
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

int32_t sai_pipe_write(sai_pipe_t *p, const void *data, uint32_t len, int32_t timeout_ms)
{
    if (p == NULL || data == NULL || p->put_wait == NULL) {
        return SAI_ERR_INVAL;
    }
    const uint8_t *src = data;
    uint32_t written = 0;

    while (written < len) {
        uint32_t key = port_lock();
        if (p->count < p->size) {
            while (written < len && p->count < p->size) {
                p->buf[(p->head + p->count) % p->size] = src[written];
                p->count++;
                written++;
            }
            port_unlock(key);
            pipe_wake(p->get_wait);
            continue;
        }
        port_unlock(key);

        if (timeout_ms == SAI_NO_WAIT) {
            break;                       /* partial write is allowed */
        }
        if (_sai_wq_block(p->put_wait, 0u, timeout_ms) != SAI_OK) {
            break;
        }
    }
    return (int32_t)written;
}

int32_t sai_pipe_read(sai_pipe_t *p, void *data, uint32_t len, int32_t timeout_ms)
{
    if (p == NULL || data == NULL || p->get_wait == NULL) {
        return SAI_ERR_INVAL;
    }
    uint8_t *dst = data;
    uint32_t nread = 0;

    while (nread < len) {
        uint32_t key = port_lock();
        if (p->count > 0) {
            while (nread < len && p->count > 0) {
                dst[nread] = p->buf[p->head];
                p->head = (p->head + 1u) % p->size;
                p->count--;
                nread++;
            }
            port_unlock(key);
            pipe_wake(p->put_wait);
            continue;
        }
        port_unlock(key);

        if (nread > 0) {
            break;                       /* partial read: return what we have */
        }
        if (timeout_ms == SAI_NO_WAIT) {
            break;
        }
        if (_sai_wq_block(p->get_wait, 0u, timeout_ms) != SAI_OK) {
            break;
        }
    }
    return (int32_t)nread;
}

uint32_t sai_pipe_count(const sai_pipe_t *p)
{
    return p ? p->count : 0u;
}

sai_status_t sai_pipe_destroy(sai_pipe_t *p)
{
    if (p == NULL) {
        return SAI_ERR_INVAL;
    }
    if (_sai_wq_count(p->put_wait) || _sai_wq_count(p->get_wait)) {
        return SAI_ERR_BUSY;
    }
    if (p->alloc) {
        sai_free(p->buf);
        p->buf = NULL;
    }
    p->kobj.type = SAI_KOBJ_NONE;
    return SAI_OK;
}
