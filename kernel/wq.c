/**
 * @file kernel/wq.c
 * @brief Sorted wait queues: the single blocking engine for every kernel
 *        primitive (mutexes, semaphores, condvars, queues, pipes, events,
 *        pools, slabs).
 *
 * Design: a waitqueue is an ordered singly-linked list of threads. Ordering
 * is by wq_key with FIFO tie-break; WQ_FIFO ignores the key. Blocking moves
 * the current thread off the ready ring onto the queue and, when a timeout
 * is given, onto the kernel's deadline list. Wakeup delivers a result code
 * (SAI_OK / SAI_ERR_TIMEOUT / custom) through t->wq_result.
 */
#include "internal.h"
#include <sai/mem.h>

#define SAI_WQ_KEY_MAX SAI_WQ_KEY_MASK

/* ------------------------------------------------------------------ */
/* Queue create/destroy                                                */
/* ------------------------------------------------------------------ */
sai_waitqueue_t *_sai_wq_create(bool fifo)
{
    /* Wait queues are tiny; they live inside their owning primitive and are
     * initialized in place. This entry point exists for dynamically created
     * primitives; it returns NULL under SAI_STATIC_KERNEL build configs. */
#if CONFIG_SAI_DYNAMIC_IPC
    sai_waitqueue_t *wq = sai_malloc(sizeof(*wq));
    if (wq != NULL) {
        wq->head = NULL;
        wq->flags = (fifo ? WQ_FIFO : 0u) | WQ_DYNAMIC;
    }
    return wq;
#else
    (void)fifo;
    return NULL;
#endif
}

void _sai_wq_destroy(sai_waitqueue_t *wq)
{
    (void)wq;   /* queues embedded in primitives need no teardown */
#if CONFIG_SAI_DYNAMIC_IPC
    if (wq != NULL && (wq->flags & WQ_DYNAMIC) != 0u) {
        sai_free(wq);
    }
#endif
}

/* ------------------------------------------------------------------ */
/* Insert / remove                                                     */
/* ------------------------------------------------------------------ */
static void wq_insert(sai_waitqueue_t *wq, sai_thread_t *t, uint32_t key)
{
    t->wq_key  = key;
    t->wq      = wq;
    t->wq_next = NULL;

    if ((wq->flags & WQ_FIFO) || wq->head == NULL) {
        if (wq->head == NULL) {
            wq->head = t;
            return;
        }
        /* FIFO: append at tail. */
        sai_thread_t *cur = wq->head;
        while (cur->wq_next != NULL) {
            cur = cur->wq_next;
        }
        cur->wq_next = t;
        return;
    }

    /* Sorted insert: ascending key; FIFO among equal keys. Keys are read
     * with SAI_WQ_KEY_MASK because the high nibble carries wake markers. */
    sai_thread_t **pp = &wq->head;
    while (*pp != NULL && (*pp)->wq_key <= key) {
        pp = &(*pp)->wq_next;
    }
    t->wq_next = *pp;
    *pp = t;
}

bool _sai_wq_remove(sai_thread_t *t)
{
    sai_waitqueue_t *wq = t->wq;
    if (wq == NULL) {
        return false;
    }
    sai_thread_t **pp = &wq->head;
    while (*pp != NULL) {
        if (*pp == t) {
            *pp = t->wq_next;
            t->wq_next = NULL;
            t->wq = NULL;
            return true;
        }
        pp = &(*pp)->wq_next;
    }
    t->wq = NULL;
    return false;
}

/* ------------------------------------------------------------------ */
/* Blocking core                                                       */
/* ------------------------------------------------------------------ */
/** Wake a waiter: dequeue, ready it, deliver result. Returns true if woken. */
static bool wq_wake(sai_waitqueue_t *wq, sai_status_t result)
{
    sai_thread_t *t = wq->head;
    if (t == NULL) {
        return false;
    }
    wq->head = t->wq_next;
    t->wq_next = NULL;
    t->wq      = NULL;
    t->wq_result = (uint32_t)result;

    uint32_t key = port_lock();
    if (t->wq_flags & SAI_WQF_TIMED) {
        _sai_deadline_remove(t);
        t->wq_flags &= (uint8_t)~SAI_WQF_TIMED;
    }
    t->state = SAI_THREAD_READY;
    _sai_ready_insert(t);
    port_unlock(key);
    return true;
}

uint32_t _sai_wq_count(const sai_waitqueue_t *wq)
{
    uint32_t n = 0;
    for (const sai_thread_t *t = wq->head; t != NULL; t = t->wq_next) {
        n++;
    }
    return n;
}

sai_status_t _sai_wq_wake_one(sai_waitqueue_t *wq, sai_status_t result)
{
    return wq_wake(wq, result) ? SAI_OK : SAI_ERR_EMPTY;
}

uint32_t _sai_wq_wake_all(sai_waitqueue_t *wq, sai_status_t result)
{
    uint32_t n = 0;
    while (wq_wake(wq, result)) {
        n++;
    }
    return n;
}

sai_status_t _sai_wq_block(sai_waitqueue_t *wq, uint32_t key, int32_t timeout_ms)
{
    sai_thread_t *t = _sai_current;

    if (t == NULL || port_in_isr()) {
        return SAI_ERR_PERM;
    }

    uint32_t key_int = port_lock();

    if (timeout_ms == SAI_NO_WAIT) {
        port_unlock(key_int);
        return SAI_ERR_WOULD_BLOCK;    /* caller decides; not enqueued */
    }

    if (wq != NULL) {
        wq_insert(wq, t, key);
    }
    t->state = SAI_THREAD_BLOCKED;     /* wq == NULL: bare block (join) */

    if (timeout_ms != SAI_WAIT_FOREVER) {
        uint32_t ticks = SAI_MS_TO_TICKS((uint32_t)timeout_ms);
        if (ticks == 0) {
            ticks = 1;                     /* minimum 1 tick on sub-tick timeouts */
        }
        t->wake_at = sai_tick_count() + ticks;
        t->wq_flags |= SAI_WQF_TIMED;
        _sai_deadline_add(t);
    }

    /* Leave the critical section (baton held once at this point) and
     * switch away; port_switch fully releases and parks this context. */
    port_unlock(key_int);
    _sai_schedule();

    /* Back from the switch: result was delivered by the waker. */
    return (sai_status_t)t->wq_result;
}

void _sai_wq_timeout(sai_thread_t *t)
{
    (void)_sai_wq_remove(t);
    t->wq_flags &= (uint8_t)~SAI_WQF_TIMED;
    t->wq_result = (uint32_t)SAI_ERR_TIMEOUT;
    t->state = SAI_THREAD_READY;
    _sai_ready_insert(t);
}

void _sai_unblock_thread(sai_thread_t *t, sai_status_t result)
{
    uint32_t key = port_lock();

    if (t->wq_flags & SAI_WQF_TIMED) {
        _sai_deadline_remove(t);
        t->wq_flags &= (uint8_t)~SAI_WQF_TIMED;
    }
    (void)_sai_wq_remove(t);
    t->wq_result = (uint32_t)result;
    t->state = SAI_THREAD_READY;
    _sai_ready_insert(t);

    port_unlock(key);
}

/* ------------------------------------------------------------------ */
/* Public waitqueue wrappers (declared in sai/sync.h)                  */
/* These operate on caller-embedded waitqueues used by the primitives. */
/* ------------------------------------------------------------------ */
#include <sai/mem.h>

/* ------------------------------------------------------------------ */
/* Deadline list (sleeping threads + timed waiters)                    */
/* ------------------------------------------------------------------ */
static sai_thread_t *_sai_deadlines = NULL;   /* sorted by wake_at */

sai_thread_t *_sai_deadline_head(void)
{
    return _sai_deadlines;
}

void _sai_deadline_add(sai_thread_t *t)
{
    sai_thread_t **pp = &_sai_deadlines;
    while (*pp != NULL && (int32_t)(int32_t)(*pp)->wake_at - (int32_t)t->wake_at <= 0) {
        pp = &(*pp)->deadline_next;
    }
    t->deadline_next = *pp;
    *pp = t;
}

void _sai_deadline_remove(sai_thread_t *t)
{
    sai_thread_t **pp = &_sai_deadlines;
    while (*pp != NULL) {
        if (*pp == t) {
            *pp = t->deadline_next;
            t->deadline_next = NULL;
            return;
        }
        pp = &(*pp)->deadline_next;
    }
}

void _sai_deadlines_tick(void)
{
    uint32_t now = _sai_tick_count;
    while (_sai_deadlines != NULL &&
           (int32_t)(_sai_deadlines->wake_at - now) <= 0) {
        sai_thread_t *t = _sai_deadlines;
        _sai_deadlines = t->deadline_next;
        t->deadline_next = NULL;
        t->wq_flags &= (uint8_t)~SAI_WQF_TIMED;

        if (t->state == SAI_THREAD_SLEEPING) {
            t->wake_at = 0;
            uint32_t key = port_lock();
            t->state = SAI_THREAD_READY;
            _sai_ready_insert(t);
            port_unlock(key);
        } else if (t->state == SAI_THREAD_BLOCKED) {
            _sai_wq_timeout(t);
        }
    }
}

