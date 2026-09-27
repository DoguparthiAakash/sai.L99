/**
 * @file kernel/thread.c
 * @brief Thread lifecycle: create, start, sleep/wake, suspend, join, delete.
 */
#include "internal.h"
#include <sai/mem.h>

/* ------------------------------------------------------------------ */
/* Registry (implemented in kernel/init.c)                             */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Stack sentinel / high-water                                         */
/* ------------------------------------------------------------------ */
#if CONFIG_SAI_STACK_SENTINEL
#define SAI_STACK_PATTERN 0xC5C5C5C5u
#endif

void _sai_stack_poison(sai_thread_t *t)
{
#if CONFIG_SAI_STACK_SENTINEL
    uint32_t words = (uint32_t)(t->stack_size / sizeof(uint32_t));
    if (words > 64u) {
        words = 64u;                       /* 256-byte guard band */
    }
    uint32_t *p = (uint32_t *)((uintptr_t)t->stack_base + t->stack_size);
    for (uint32_t i = 0; i < words; i++) {
        *--p = SAI_STACK_PATTERN;
    }
#else
    (void)t;
#endif
}

uint32_t _sai_stack_used(const sai_thread_t *t)
{
#if CONFIG_SAI_STACK_SENTINEL
    uint32_t *top = (uint32_t *)((uintptr_t)t->stack_base + t->stack_size);
    uint32_t words = (uint32_t)(t->stack_size / sizeof(uint32_t));
    uint32_t used = words;
    for (uint32_t i = 0; i < words; i++) {
        if (top[-1 - (ptrdiff_t)i] != SAI_STACK_PATTERN) {
            used = i;
            break;
        }
    }
    return used * (uint32_t)sizeof(uint32_t);
#else
    (void)t;
    return 0u;
#endif
}

/* ------------------------------------------------------------------ */
/* Zombies (dynamic TCBs/stacks are freed by the idle thread)          */
/* ------------------------------------------------------------------ */
sai_thread_t *_sai_zombies = NULL;

void _sai_zombie_reap(void)
{
    for (;;) {
        uint32_t key = port_lock();
        sai_thread_t *z = _sai_zombies;
        if (z != NULL) {
            _sai_zombies = z->join_next;
        }
        port_unlock(key);
        if (z == NULL) {
            return;
        }
        if (z->flags & SAI_THREAD_FLAG_DYNAMIC) {
            if (z->flags & SAI_THREAD_FLAG_STACK_DYN) {
                sai_free(z->stack_base);
            }
            sai_free(z);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Create / spawn / start                                              */
/* ------------------------------------------------------------------ */
sai_status_t sai_thread_create(sai_thread_t *t, const char *name,
                               sai_thread_entry_t entry, void *arg,
                               uint8_t prio, void *stack, size_t stack_size,
                               uint32_t flags)
{
    if (t == NULL || entry == NULL || prio >= SAI_IDLE_PRIORITY) {
        return SAI_ERR_INVAL;
    }
    memset(t, 0, sizeof(*t));
    t->kobj.type = SAI_KOBJ_THREAD;
    t->kobj.name = name ? name : "thread";
    t->entry      = entry;
    t->arg        = arg;
    t->prio       = prio;
    t->base_prio  = prio;
    t->state      = SAI_THREAD_SUSPENDED;
    t->sched_mode = (flags & SAI_THREAD_COOPERATIVE) ? 1u : 0u;

    if (stack_size == 0) {
        stack_size = SAI_THREAD_STACK_DEFAULT;
    }
    stack_size = (stack_size + SAI_STACK_ALIGN - 1u) & ~(size_t)(SAI_STACK_ALIGN - 1u);
    t->stack_size = stack_size;

    if (stack == NULL) {
        uint8_t *mem = sai_malloc(stack_size);
        if (mem == NULL) {
            return SAI_ERR_NOMEM;
        }
        t->stack_base = mem;
        t->flags |= SAI_THREAD_FLAG_STACK_DYN;
    } else {
        t->stack_base = stack;
    }

    _sai_stack_poison(t);
    return SAI_OK;
}

sai_thread_t *sai_thread_spawn(const char *name, sai_thread_entry_t entry,
                               void *arg, uint8_t prio, size_t stack_size,
                               uint32_t flags)
{
    sai_thread_t *t = sai_malloc(sizeof(*t));
    if (t == NULL) {
        return NULL;
    }
    if (sai_thread_create(t, name, entry, arg, prio, NULL, stack_size, flags) != SAI_OK) {
        sai_free(t);
        return NULL;
    }
    t->flags |= SAI_THREAD_FLAG_DYNAMIC;
    if (sai_thread_start(t) != SAI_OK) {
        _sai_zombies = t;         /* idle will free it */
        return NULL;
    }
    return t;
}

sai_status_t sai_thread_start(sai_thread_t *t)
{
    if (t == NULL || t->entry == NULL || t->state != SAI_THREAD_SUSPENDED) {
        return SAI_ERR_STATE;
    }
    port_thread_ready(t);                  /* host: spawn native thread    */
    if (t->sp == NULL) {
        t->sp = port_stack_init((uint8_t *)t->stack_base + t->stack_size,
                                t->stack_size, t->entry, t->arg);
    }
    _sai_ready_thread(t);

    if (t->prio < (_sai_current ? _sai_current->prio : 255u)) {
        _sai_schedule();
    }
    return SAI_OK;
}

/* ------------------------------------------------------------------ */
/* Sleep / wake                                                        */
/* ------------------------------------------------------------------ */
sai_status_t _sai_thread_sleep_ticks(sai_thread_t *t, uint32_t ticks)
{
    if (ticks == 0) {
        sai_yield();
        return SAI_OK;
    }
    uint32_t key = port_lock();
    if (t->state != SAI_THREAD_READY && t->state != SAI_THREAD_RUNNING) {
        port_unlock(key);
        return SAI_ERR_STATE;
    }
    _sai_ready_remove(t);
    t->wake_at = sai_tick_count() + ticks;
    t->wq_flags |= SAI_WQF_TIMED;
    t->state   = SAI_THREAD_SLEEPING;
    _sai_deadline_add(t);                  /* timer expiry makes us ready */
    port_unlock(key);

    _sai_schedule();                       /* switch away; wake on timer */
    return SAI_OK;
}

sai_status_t sai_sleep_ticks(uint32_t ticks)
{
    return _sai_thread_sleep_ticks(_sai_current, ticks);
}

sai_status_t sai_sleep(uint32_t ms)
{
    return sai_sleep_ticks(SAI_MS_TO_TICKS(ms));
}

sai_status_t sai_thread_sleep(sai_thread_t *t, uint32_t ms)
{
    if (t == NULL) {
        return SAI_ERR_INVAL;
    }
    uint32_t ticks = SAI_MS_TO_TICKS(ms);
    if (t == _sai_current) {
        return _sai_thread_sleep_ticks(t, ticks);
    }
    uint32_t key = port_lock();
    if (t->state != SAI_THREAD_READY && t->state != SAI_THREAD_RUNNING) {
        port_unlock(key);
        return SAI_ERR_STATE;
    }
    _sai_ready_remove(t);
    t->wake_at = sai_tick_count() + ticks;
    t->wq_flags |= SAI_WQF_TIMED;
    t->state   = SAI_THREAD_SLEEPING;
    _sai_deadline_add(t);
    port_unlock(key);
    return SAI_OK;
}

sai_status_t _sai_thread_wake_now(sai_thread_t *t)
{
    uint32_t key = port_lock();
    if (t->state == SAI_THREAD_SLEEPING) {
        t->wake_at = 0;
        if (t->wq_flags & SAI_WQF_TIMED) {
            _sai_deadline_remove(t);       /* cancel the pending expiry */
            t->wq_flags &= (uint8_t)~SAI_WQF_TIMED;
        }
        _sai_make_ready_locked(t);
        port_unlock(key);
        return SAI_OK;
    }
    port_unlock(key);
    return SAI_ERR_STATE;
}

sai_status_t sai_thread_wake(sai_thread_t *t)
{
    if (t == NULL) {
        return SAI_ERR_INVAL;
    }
    if (_sai_thread_wake_now(t) != SAI_OK) {
        return SAI_ERR_STATE;
    }
    if (t->prio < (_sai_current ? _sai_current->prio : 255u)) {
        _sai_schedule();
    }
    return SAI_OK;
}

void _sai_thread_sleep_expired(sai_thread_t *t)
{
    uint32_t key = port_lock();
    if (t->state == SAI_THREAD_SLEEPING) {
        t->wake_at = 0;
        _sai_make_ready_locked(t);
        port_unlock(key);
        return;
    }
    port_unlock(key);
}

/* ------------------------------------------------------------------ */
/* Suspend / resume                                                    */
/* ------------------------------------------------------------------ */
sai_status_t sai_thread_suspend(sai_thread_t *t)
{
    if (t == NULL) {
        return SAI_ERR_INVAL;
    }
    uint32_t key = port_lock();
    if (t->state == SAI_THREAD_READY || t->state == SAI_THREAD_RUNNING) {
        _sai_ready_remove(t);
    } else if (t->state == SAI_THREAD_SLEEPING) {
        t->wake_at = 0;                    /* timer expiry ignores non-sleepers */
        if (t->wq_flags & SAI_WQF_TIMED) {
            _sai_deadline_remove(t);
            t->wq_flags &= (uint8_t)~SAI_WQF_TIMED;
        }
    }
    t->state = SAI_THREAD_SUSPENDED;
    port_unlock(key);

    if (t == _sai_current) {
        _sai_schedule();
    }
    return SAI_OK;
}

sai_status_t sai_thread_resume(sai_thread_t *t)
{
    if (t == NULL) {
        return SAI_ERR_INVAL;
    }
    uint32_t key = port_lock();
    if (t->state != SAI_THREAD_SUSPENDED) {
        port_unlock(key);
        return SAI_ERR_STATE;
    }
    _sai_make_ready_locked(t);
    port_unlock(key);

    if (t->prio < (_sai_current ? _sai_current->prio : 255u)) {
        _sai_schedule();
    }
    return SAI_OK;
}

/* ------------------------------------------------------------------ */
/* Priority                                                            */
/* ------------------------------------------------------------------ */
sai_status_t sai_thread_set_priority(sai_thread_t *t, uint8_t prio)
{
    if (t == NULL || prio >= SAI_IDLE_PRIORITY) {
        return SAI_ERR_INVAL;
    }
    uint32_t key = port_lock();

    bool was_ready = (t->state == SAI_THREAD_READY);
    if (was_ready) {
        _sai_ready_remove(t);              /* re-bin into new priority ring */
    }
    t->base_prio = prio;
    t->prio     = prio;                    /* PI recompute happens in mutex */
    if (was_ready) {
        t->state = SAI_THREAD_READY;
        _sai_ready_insert(t);
    }
    port_unlock(key);

    if (_sai_current && t->prio < _sai_current->prio) {
        _sai_schedule();
    }
    return SAI_OK;
}

/* ------------------------------------------------------------------ */
/* Join                                                                */
/* ------------------------------------------------------------------ */
sai_status_t sai_thread_join(sai_thread_t *t, int32_t timeout_ms)
{
    if (t == NULL || t == _sai_current) {
        return SAI_ERR_INVAL;
    }
    uint32_t key = port_lock();
    if (t->state == SAI_THREAD_DEAD) {
        port_unlock(key);
        t->flags |= SAI_THREAD_FLAG_JOINED;
        return SAI_OK;
    }
    t->join_next = _sai_current;           /* joiner list on the target     */
    _sai_current->wq_next = NULL;
    port_unlock(key);

    /* Block without a queue: the target's exit path readies us directly. */
    sai_status_t rc = _sai_wq_block(NULL, 0u, timeout_ms);
    return rc;
}

/* Called by the dying thread to release joiners. */
void _sai_thread_notify_joiners(sai_thread_t *t)
{
    sai_thread_t *j = t->join_next;
    t->join_next = NULL;
    while (j != NULL) {
        sai_thread_t *next = j->wq_next;
        j->wq_next = NULL;
        j->wq_result = (uint32_t)SAI_OK;
        _sai_unblock_thread(j, SAI_OK);    /* detach + ready + preempt hint */
        j = next;
    }
}

/* ------------------------------------------------------------------ */
/* Exit / delete                                                       */
/* ------------------------------------------------------------------ */
void _sai_thread_cleanup_and_exit(void)
{
    sai_thread_t *t = _sai_current;
    uint32_t key = port_lock();

    _sai_ready_remove(t);
    t->state = SAI_THREAD_DEAD;

    /* Release any joiners, then queue the TCB for the idle reaper if it is
     * dynamic. Stacks of static threads are left alone. */
    _sai_thread_notify_joiners(t);
    if (t->flags & SAI_THREAD_FLAG_DYNAMIC) {
        t->flags |= SAI_THREAD_FLAG_DYING;
        t->join_next = _sai_zombies;
        _sai_zombies = t;
    }
    port_unlock(key);

    _sai_schedule();                       /* never returns to this thread */
    for (;;) {
    }
}

void sai_thread_exit(void)
{
    _sai_thread_cleanup_and_exit();
}

sai_status_t sai_thread_delete(sai_thread_t *t)
{
    if (t == NULL || t == _sai_current) {
        return SAI_ERR_INVAL;
    }
    uint32_t key = port_lock();
    switch (t->state) {
    case SAI_THREAD_READY:
    case SAI_THREAD_RUNNING:
    case SAI_THREAD_SUSPENDED:
        break;
    default:
        port_unlock(key);
        return SAI_ERR_STATE;          /* blocked/sleeping: refuse for now */
    }
    _sai_ready_remove(t);
    t->state = SAI_THREAD_DEAD;
    _sai_thread_notify_joiners(t);
    if (t->flags & SAI_THREAD_FLAG_DYNAMIC) {
        t->flags |= SAI_THREAD_FLAG_DYING;
        t->join_next = _sai_zombies;
        _sai_zombies = t;
    }
    port_unlock(key);

    if (t->prio < (_sai_current ? _sai_current->prio : 255u)) {
        _sai_schedule();
    }
    return SAI_OK;
}

/* ------------------------------------------------------------------ */
/* ISR signaling                                                       */
/* ------------------------------------------------------------------ */
sai_status_t sai_isr_wake(sai_thread_t *t)
{
    if (t == NULL) {
        return SAI_ERR_INVAL;
    }
    uint32_t key = port_lock();
    if (t->state == SAI_THREAD_SLEEPING || t->state == SAI_THREAD_SUSPENDED) {
        t->wake_at = 0;
        _sai_make_ready_locked(t);
        _sai_isr_reschedules++;
        port_unlock(key);
        return SAI_OK;
    }
    port_unlock(key);
    return SAI_ERR_STATE;
}

/* ------------------------------------------------------------------ */
/* Info / getters / yield                                              */
/* ------------------------------------------------------------------ */
sai_thread_t *sai_current_thread(void) { return _sai_current; }

uint8_t sai_thread_get_priority(const sai_thread_t *t) { return t->base_prio; }
uint8_t sai_thread_get_effective_priority(const sai_thread_t *t) { return t->prio; }

void sai_thread_get_info(const sai_thread_t *t, sai_thread_info_t *out)
{
    out->name        = t->kobj.name;
    out->prio        = t->prio;
    out->base_prio   = t->base_prio;
    out->state       = t->state;
    out->sched_mode  = t->sched_mode;
    out->stack_size  = (uint32_t)t->stack_size;
    out->stack_used  = _sai_stack_used(t);
}

void sai_yield(void)
{
    uint32_t key = port_lock();
    _sai_current->state = SAI_THREAD_READY;
    _sai_ready_insert(_sai_current);   /* move to tail of own prio ring */
    port_unlock(key);
    _sai_schedule();
}

/* ------------------------------------------------------------------ */
/* Kernel lifecycle: sai_kernel_init lives in sched.c.                 */
/* ------------------------------------------------------------------ */

