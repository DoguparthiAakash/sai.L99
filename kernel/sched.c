/**
 * @file kernel/sched.c
 * @brief Preemptive priority scheduler with round-robin time slicing and an
 *        alternate cooperative mode.
 *
 * Scheduling model (rotation rings):
 *  - _sai_ready[p] is the head of a circular ready list for priority p; the
 *    head is the thread that has been waiting longest ("next to run").
 *  - _sai_current is the RUNNING thread; it is NOT in any ready ring.
 *  - _sai_schedule_pick() is the single scheduling decision point:
 *      * if the running thread is still runnable and nothing higher has
 *        appeared, it may keep running (no switch at all);
 *      * otherwise the running thread is re-queued at the TAIL of its ring
 *        (round-robin) or left off (blocked/dead), and the head of the
 *        highest non-empty ring becomes _sai_current.
 *  - Both the host port (baton switch) and the ARM port (PendSV) call this
 *    one function, so both share identical scheduling semantics.
 *
 * Preemption policy: after every wake/ready operation the kernel records a
 * hint; the port delivers the switch (baton hand-off / PendSV) either
 * immediately (_sai_reschedule, from thread context) or at the next
 * port_unlock() tail (from "interrupt" context).
 *
 * Invariants:
 *  - A thread is on exactly one of: ready ring, wait queue, deadline list,
 *    or is dead. The running thread is in no ring.
 *  - Ready-queue mutations happen under port_lock() (interrupts disabled).
 */
#include "internal.h"
#include <sai/budget.h>
#include <sai/trace.h>
#if defined(_MSC_VER)
#include <intrin.h>                    /* _BitScanForward */
#endif

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */
sai_thread_t *_sai_current = NULL;            /* the running thread        */
volatile uint32_t _sai_ready_bitmap = 0;
sai_thread_t *_sai_ready[SAI_NUM_PRIORITIES] = { 0 };
volatile bool _sai_preempt_hint = false;
volatile uint8_t _sai_rotate_prio = SAI_NUM_PRIORITIES;   /* rotation request */
uint32_t _sai_timeslice = 10;
volatile bool _sai_started = false;
uint8_t _sai_sched_mode = SAI_SCHED_PREEMPTIVE;
volatile uint32_t _sai_ctx_switches = 0;
volatile uint32_t _sai_isr_reschedules = 0;

sai_thread_t _sai_idle_thread;
sai_thread_t _sai_main_thread;
uint8_t _sai_idle_stack[CONFIG_SAI_IDLE_STACK_SIZE];
#ifndef SAI_HOST_BUILD
/* Target builds run the boot context (reset-time MSP) only until the first
 * PendSV switch; the main thread then runs its own dedicated stack. */
static uint8_t _sai_main_stack[CONFIG_SAI_MAIN_STACK_SIZE];
#endif

/* ------------------------------------------------------------------ */
/* Ready-ring operations                                               */
/* ------------------------------------------------------------------ */
/* Each ready "list" is a circular FIFO: _sai_ready[p] is the OLDEST thread
 * at that priority (next to run); new arrivals append at the tail. */
void _sai_ready_insert(sai_thread_t *t)
{
    uint8_t p = t->prio;
    SAI_DASSERT(p < SAI_NUM_PRIORITIES);

    if (_sai_ready[p] == NULL) {
        _sai_ready[p] = t;
        t->prio_next = t;                    /* single-element ring */
    } else {
        sai_thread_t *head = _sai_ready[p];
        t->prio_next = head->prio_next;
        head->prio_next = t;
    }
    _sai_ready_bitmap |= (1u << p);
}

void _sai_ready_remove(sai_thread_t *t)
{
    uint8_t p = t->prio;
    SAI_DASSERT(p < SAI_NUM_PRIORITIES);

    if (_sai_ready[p] == NULL) {
        return;
    }
    sai_thread_t *cur = _sai_ready[p];
    sai_thread_t *prev = cur;
    do {
        if (cur == t) {
            if (cur == prev) {                    /* only element */
                _sai_ready[p] = NULL;
                _sai_ready_bitmap &= ~(1u << p);
            } else {
                prev->prio_next = cur->prio_next;
                if (_sai_ready[p] == t) {
                    _sai_ready[p] = prev->prio_next;   /* promote successor */
                }
            }
            t->prio_next = NULL;
            return;
        }
        prev = cur;
        cur = cur->prio_next;
    } while (cur != _sai_ready[p] && cur != t);
}

/** Highest-priority ready thread (the next to run), or NULL. */
sai_thread_t *_sai_ready_head(void)
{
    uint32_t bm = _sai_ready_bitmap;
    if (bm == 0u) {
        return NULL;
    }
#if defined(__GNUC__)
    uint8_t p = (uint8_t)__builtin_ctz(bm);        /* lowest set bit = highest prio */
#elif defined(_MSC_VER)
    unsigned long idx;
    _BitScanForward(&idx, bm);                     /* bm != 0 here */
    uint8_t p = (uint8_t)idx;
#else
    uint8_t p = 0;
    while (((bm >> p) & 1u) == 0u) {
        p++;
    }
#endif
    return _sai_ready[p];
}

/* ------------------------------------------------------------------ */
/* Scheduler API (internal)                                            */
/* ------------------------------------------------------------------ */
void _sai_make_ready_locked(sai_thread_t *t)
{
    if (t->state == SAI_THREAD_READY || t->state == SAI_THREAD_RUNNING) {
        return;
    }
    /* A thread being readied can no longer expire: drop any deadline
     * registration (sleep or timed block) it still carries. */
    if (t->wq_flags & SAI_WQF_TIMED) {
        _sai_deadline_remove(t);
        t->wq_flags &= (uint8_t)~SAI_WQF_TIMED;
    }
    t->wq = NULL;
    t->wake_at = 0;
    t->state = SAI_THREAD_READY;
    _sai_ready_insert(t);
    _sai_preempt_hint = (_sai_current != NULL && t->prio < _sai_current->prio);
}

void _sai_ready_thread(sai_thread_t *t)
{
    uint32_t key = port_lock();
    _sai_make_ready_locked(t);
    port_unlock(key);
}

void _sai_remove_ready(sai_thread_t *t)
{
    uint32_t key = port_lock();
    _sai_ready_remove(t);
    port_unlock(key);
}

/* ------------------------------------------------------------------ */
/* Tick                                                                */
/* ------------------------------------------------------------------ */
void _sai_sched_tick(void)
{
    sai_thread_t *cur = _sai_current;
    if (cur == NULL) {
        return;
    }
#if CONFIG_SAI_THREAD_STATS
    cur->ctx_switches++;              /* tick on this thread: stats     */
#endif
#if CONFIG_SAI_BUDGET
    _sai_budget_on_tick(cur);
#endif
    if (_sai_sched_mode == SAI_SCHED_COOPERATIVE || cur->sched_mode != 0) {
        return;                             /* no preempt in cooperative mode */
    }
    if (cur->prio == SAI_IDLE_PRIORITY) {
        return;                             /* idle thread: no timeslice    */
    }
    if (_sai_timeslice == 0) {
        return;                             /* slicing disabled             */
    }
    if (cur->timeslice > 0) {
        cur->timeslice--;
    }
    if (cur->timeslice == 0) {
        cur->timeslice = _sai_timeslice;    /* rotate at next pick */
        if (_sai_ready[cur->prio] != NULL) {
            _sai_rotate_prio = cur->prio;   /* request a round-robin pick  */
            port_schedule_from_isr();       /* peers at same prio: rotate  */
        }
    }
}

/* ------------------------------------------------------------------ */
/* Reschedule                                                          */
/* ------------------------------------------------------------------ */
/**
 * One scheduling decision.  Updates _sai_current; returns the thread that
 * should run next.  Called with interrupts disabled (baton held).
 *
 * Policy: the running thread KEEPS the CPU unless
 *   - it is no longer runnable (blocked / sleeping / dead), or
 *   - a strictly higher-priority thread is ready (preemption), or
 *   - a round-robin rotation was requested for its priority (tick).
 *
 *   next == cur  -> keep running (no switch requested)
 *   next != cur  -> caller performs the context switch to next.  A displaced
 *                   still-RUNNING runner is re-queued here (supervisor-side
 *                   eviction); a yielding/blocking runner was queued by its
 *                   own path and is NOT re-queued here.
 *   NULL return  -> pre-start edge case only.
 */
sai_thread_t *_sai_schedule_pick(void)
{
    sai_thread_t *cur = _sai_current;
    sai_thread_t *best = _sai_ready_head();
    sai_thread_t *from = cur;           /* for the trace event */

    bool switch_needed;
    if (best == NULL) {
        switch_needed = (cur == NULL) ||
                        (cur != &_sai_idle_thread &&
                         cur->state != SAI_THREAD_RUNNING);
    } else if (cur == NULL) {
        switch_needed = true;               /* boot: dispatch first pick */
    } else if (cur->state != SAI_THREAD_RUNNING) {
        switch_needed = true;               /* runner blocked/slept/died */
    } else if (best->prio < cur->prio) {
        switch_needed = true;               /* preempted by higher prio  */
    } else if (best->prio == cur->prio && _sai_rotate_prio == cur->prio) {
        switch_needed = true;               /* timeslice rotation        */
    } else {
        switch_needed = false;              /* runner stays in charge    */
    }

    if (!switch_needed) {
        return cur;
    }
    _sai_rotate_prio = SAI_NUM_PRIORITIES;  /* consume rotation request */

    /* Displace a still-RUNNING runner (supervisor eviction / boot): re-queue
     * it at the tail of its own priority ring. */
    if (cur != NULL && cur->state == SAI_THREAD_RUNNING &&
        cur != &_sai_idle_thread) {
        _sai_ready_remove(cur);             /* no-op if not queued */
        cur->state = SAI_THREAD_READY;
        cur->timeslice = _sai_timeslice;
        _sai_ready_insert(cur);
    }

    if (best == NULL) {
        /* Nothing ready: run the idle thread (kept out of the ready rings).
         * Fast path: the idle thread keeps the CPU without re-picking. */
        if (cur == &_sai_idle_thread) {
            return cur;
        }
#if CONFIG_SAI_BUDGET
        if (cur != NULL) {
            _sai_budget_on_switch_out(cur);   /* idle entry ends the run */
        }
#endif
        _sai_idle_thread.state = SAI_THREAD_RUNNING;
        _sai_current = &_sai_idle_thread;
        _sai_ctx_switches++;
#if CONFIG_SAI_TRACE
        _sai_trace_event((uint8_t)SAI_TRACE_SWITCH,
                         (uint32_t)(uintptr_t)&_sai_idle_thread,
                         (uint32_t)(uintptr_t)cur, 0u);
#endif
        return _sai_current;
    }

    _sai_ready_remove(best);
    best->state = SAI_THREAD_RUNNING;
    best->timeslice = _sai_timeslice;
    _sai_current = best;
    _sai_ctx_switches++;
#if CONFIG_SAI_THREAD_STATS
    best->ctx_switches++;
    best->last_ran_tick = sai_tick_count();
#endif
#if CONFIG_SAI_BUDGET
    if (from != NULL) {
        _sai_budget_on_switch_out(from);
    }
    /* Anchor the run-start at the dispatch tick: switch latency between
     * decision and first tick must not be billed to either thread. */
    best->budget_run_start = sai_tick_count();
#endif
#if CONFIG_SAI_TRACE
    _sai_trace_event((uint8_t)SAI_TRACE_SWITCH,
                     (uint32_t)(uintptr_t)best,
                     (uint32_t)(uintptr_t)from, 0u);
#endif
    return best;
}

/**
 * Cooperatively yield the CPU from a thread (block, sleep, yield, preempt).
 * CONTRACT: the caller has already removed this thread from the ready ring
 * and set its state (BLOCKED/SLEEPING/...) before calling, and holds no
 * port_lock() on entry (it must have released it).
 *
 * Runs the pick and, if another thread was selected, performs the
 * context switch (host: baton park; ARM: PendSV-ish direct switch).
 */
void _sai_schedule(void)
{
    if (!_sai_started) {
        return;                             /* scheduler not up yet */
    }
    sai_thread_t *prev = _sai_current;

    /* Pick under the port lock; the pick updates _sai_current.  The switch
     * is performed while still holding the lock: port_switch hands the
     * runner role to 'next', fully releases the lock and parks this
     * context until it is scheduled again. */
    uint32_t key = port_lock();
    sai_thread_t *next = _sai_schedule_pick();
    if (next == NULL || next == prev || prev == NULL) {
        port_unlock(key);                   /* kept running: plain release */
        return;
    }
    port_switch(prev, next);                /* returns when we run again  */
}

/**
 * Preemption check at unlock boundaries / after wakeup events: if a hint
 * fired and a higher-priority thread is ready, switch to it.
 */
void _sai_reschedule(void)
{
    if (!_sai_started || _sai_current == NULL) {
        return;
    }
    if (!_sai_preempt_hint) {
        return;
    }
    sai_thread_t *prev = _sai_current;

    uint32_t key = port_lock();
    _sai_preempt_hint = false;
    sai_thread_t *next = _sai_schedule_pick();
    if (next == NULL || next == prev) {
        port_unlock(key);
        return;
    }
    port_switch(prev, next);
}

/* ------------------------------------------------------------------ */
/* Kernel lifecycle                                                    */
/* ------------------------------------------------------------------ */
sai_status_t sai_kernel_init(void)
{
    static bool done;
    if (done) {
        return SAI_OK;
    }
    done = true;

    if (sai_mem_init() != SAI_OK) {
        return SAI_ERR_NOMEM;
    }
    port_init();
    return SAI_OK;
}

static void idle_thread(void *arg)
{
    (void)arg;
    while (1) {
        _sai_zombie_reap();
        port_idle_wait_for_interrupt();
    }
}

static void main_thread_entry(void *arg)
{
    sai_main_fn_t fn = (sai_main_fn_t)(void *)arg;
    if (fn != NULL) {
        fn();
    }
    /* main() returned: orderly shutdown of the whole kernel. */
    port_main_returned();
    for (;;) {                              /* port_main_returned never returns */
    }
}

void sai_kernel_start(sai_main_fn_t main_fn)
{
    uint32_t key = port_lock();

    if (_sai_started) {
        port_unlock(key);
        return;
    }

    /* Main thread: runs main_fn at priority 1 (just below cooperative
     * app threads at 0, above everything the user creates). */
    memset(&_sai_main_thread, 0, sizeof(_sai_main_thread));
    _sai_main_thread.kobj.type = SAI_KOBJ_THREAD;
    _sai_main_thread.kobj.name = "main";
    _sai_kobj_register_full(&_sai_main_thread.kobj, SAI_KOBJ_THREAD, "main");
    _sai_main_thread.entry      = main_thread_entry;
    _sai_main_thread.arg        = (void *)(uintptr_t)main_fn;
    _sai_main_thread.prio       = 1;
    _sai_main_thread.base_prio  = 1;
    _sai_main_thread.state      = SAI_THREAD_READY;
    _sai_main_thread.timeslice  = _sai_timeslice;
    _sai_stack_poison(&_sai_main_thread);

#ifndef SAI_HOST_BUILD
    /* Target boot: this calling context runs only until the first PendSV
     * switch, so give the main thread a proper initial frame on its stack
     * instead of inheriting the reset-time MSP. */
    _sai_main_thread.stack_base = _sai_main_stack;
    _sai_main_thread.stack_size = sizeof(_sai_main_stack);
    _sai_main_thread.sp = port_stack_init(
        _sai_main_stack + sizeof(_sai_main_stack),
        sizeof(_sai_main_stack), main_thread_entry,
        (void *)(uintptr_t)main_fn);
    port_thread_ready(&_sai_main_thread);
#endif

    /* Idle thread: static TCB + static stack at the lowest priority. */
    memset(&_sai_idle_thread, 0, sizeof(_sai_idle_thread));
    _sai_idle_thread.kobj.type = SAI_KOBJ_THREAD;
    _sai_idle_thread.kobj.name = "idle";
    _sai_kobj_register_full(&_sai_idle_thread.kobj, SAI_KOBJ_THREAD, "idle");
    _sai_idle_thread.entry    = idle_thread;
    _sai_idle_thread.arg      = NULL;
    _sai_idle_thread.prio     = SAI_IDLE_PRIORITY;
    _sai_idle_thread.base_prio = SAI_IDLE_PRIORITY;
    _sai_idle_thread.state    = SAI_THREAD_READY;
    _sai_idle_thread.stack_base = _sai_idle_stack;
    _sai_idle_thread.stack_size = CONFIG_SAI_IDLE_STACK_SIZE;
    _sai_idle_thread.timeslice = _sai_timeslice;
    _sai_stack_poison(&_sai_idle_thread);

#ifndef SAI_HOST_BUILD
    /* Idle needs an initial frame too: it is entered via PendSV like any
     * other thread and has no native context to inherit. */
    _sai_idle_thread.sp = port_stack_init(
        _sai_idle_stack + CONFIG_SAI_IDLE_STACK_SIZE,
        CONFIG_SAI_IDLE_STACK_SIZE, idle_thread, NULL);
    port_thread_ready(&_sai_idle_thread);
#endif

    port_timer_setup(SAI_TICK_MS);
    _sai_started = true;

    /* Boot: main becomes _sai_current and runs main_fn on the calling
     * native context (host port) / on PSP (ARM port). */
    _sai_current = &_sai_main_thread;
    _sai_main_thread.state = SAI_THREAD_RUNNING;
    port_unlock(key);

    port_start_first_thread(&_sai_main_thread);
}

bool sai_kernel_started(void) { return _sai_started; }

uint32_t sai_ctx_switches(void) { return _sai_ctx_switches; }
uint32_t sai_isr_reschedules(void) { return _sai_isr_reschedules; }

/* ------------------------------------------------------------------ */
/* Mode / timeslice setters                                            */
/* ------------------------------------------------------------------ */
void sai_sched_set_mode(sai_sched_mode_t mode)
{
    uint32_t key = port_lock();
    _sai_sched_mode = (uint8_t)mode;
    port_unlock(key);
}

sai_sched_mode_t sai_sched_get_mode(void) { return (sai_sched_mode_t)_sai_sched_mode; }

void sai_sched_set_timeslice(uint32_t ticks)
{
    uint32_t key = port_lock();
    _sai_timeslice = ticks;
    port_unlock(key);
}

uint32_t sai_sched_get_timeslice(void) { return _sai_timeslice; }
