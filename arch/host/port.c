/**
 * @file arch/host/port.c
 * @brief Host (Windows/Linux) port of the sai.L99 kernel.
 *
 * Execution model
 * ---------------
 * Threads are real native threads, but exactly one SAI thread runs at a
 * time.  The right to run is the "baton" (a mutex).  A native context may
 * enter the kernel (port_lock) only when the scheduler has named it the
 * runner (s_runner == its record); kernel switches hand the baton to the
 * exact successor chosen by _sai_schedule_pick().
 *
 *  - The main() native thread BECOMES the "main" SAI thread: after
 *    sai_kernel_start(main_fn) it simply calls main_fn on this context.
 *  - Every spawned SAI thread runs on its own native thread, parked until
 *    named runner by a hand-off.
 *  - A supervisor native thread owns wall-clock time: it advances kernel
 *    ticks, reaps zombies and hands the baton to whichever thread the
 *    scheduler pick selected.
 *
 * Lock/depth rules (mirroring the ARM interrupt model):
 *  - port_lock()  = disable interrupts: acquire baton (recursive, depth
 *    tracked) -- but only if we are the named runner.
 *  - port_unlock(key) = one interrupt-enable level; at the outermost level
 *    a pending "IRQ tail" runs one scheduling decision, and if the pick
 *    displaced the caller, port_switch() parks it.
 *  - port_switch() is called with interrupts (baton) held and returns with
 *    them fully enabled: it releases the baton completely before parking
 *    and re-acquires cleanly on resume (depth 0).
 */
#include <sai/host.h>
#include "port_internal.h"

#include <stdlib.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>
#endif

/* ------------------------------------------------------------------ */
/* Native primitives                                                   */
/* ------------------------------------------------------------------ */
#ifdef _WIN32
typedef CRITICAL_SECTION   native_mutex_t;
typedef CONDITION_VARIABLE native_cond_t;
typedef HANDLE             native_thread_t;

static void native_mutex_init(native_mutex_t *m) { InitializeCriticalSection(m); }
static void native_mutex_lock(native_mutex_t *m) { EnterCriticalSection(m); }
static void native_mutex_unlock(native_mutex_t *m) { LeaveCriticalSection(m); }
static void native_cond_init(native_cond_t *c) { InitializeConditionVariable(c); }
static void native_cond_wait(native_cond_t *c, native_mutex_t *m) { SleepConditionVariableCS(c, m, INFINITE); }
static void native_cond_signal(native_cond_t *c) { WakeConditionVariable(c); }
static void native_sleep_ms(uint32_t ms) { Sleep(ms); }
#else
typedef pthread_mutex_t    native_mutex_t;
typedef pthread_cond_t     native_cond_t;
typedef pthread_t          native_thread_t;

static void native_mutex_init(native_mutex_t *m)
{
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    /* The kernel nests critical sections; the baton must be recursive. */
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(m, &a);
    pthread_mutexattr_destroy(&a);
}
static void native_mutex_lock(native_mutex_t *m) { pthread_mutex_lock(m); }
static void native_mutex_unlock(native_mutex_t *m) { pthread_mutex_unlock(m); }
static void native_cond_init(native_cond_t *c) { pthread_cond_init(c, NULL); }
static void native_cond_wait(native_cond_t *c, native_mutex_t *m) { pthread_cond_wait(c, m); }
static void native_cond_signal(native_cond_t *c) { pthread_cond_broadcast(c); }
static void native_sleep_ms(uint32_t ms)
{
    struct timespec ts = { (time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L };
    nanosleep(&ts, NULL);
}
#endif

/* ------------------------------------------------------------------ */
/* Port state                                                          */
/* ------------------------------------------------------------------ */
typedef struct host_thread {
    sai_thread_t    *sai;          /* owning SAI thread                     */
    native_thread_t  handle;       /* native thread (valid once spawned)    */
    native_cond_t    run_cv;       /* woken when s_runner becomes me        */
    bool             started;      /* native thread was created             */
    bool             parked;       /* waiting for the runner role           */
    struct host_thread *next;
} host_thread_t;

static native_mutex_t s_lock;        /* protects port state below            */
static native_mutex_t s_baton;       /* the "who may run / IRQ depth" mutex  */
static host_thread_t *s_threads;     /* all live host thread records         */
static host_thread_t *s_runner;      /* record allowed to run (or NULL)      */
static host_thread_t s_main_rec;     /* record for main()'s native thread    */

/* Baton hold depth of the current baton owner (baton is exclusive, so a
 * single counter is safe; only its holder touches it). */
static uint32_t s_depth;

/* Which host record is the calling native context (NULL on the supervisor
 * and on any pre-kernel native thread). */
#if defined(_MSC_VER)
static __declspec(thread) host_thread_t *t_self;
#else
static __thread host_thread_t *t_self;
#endif

static volatile uint32_t s_isr_pending;
static host_isr_entry_t s_isrs[SAI_HOST_MAX_ISRS];
static uint32_t s_isr_count_total;

static volatile bool s_timer_run = false;
static native_thread_t s_timer_thread;
static bool s_timer_thread_valid;
static bool s_inited;
static int  s_exit_code;

void sai_host_port_init(void) { /* kept for API compat; init happens lazily */ }
void sai_host_set_exit_code(int code) { s_exit_code = code; }

/* ------------------------------------------------------------------ */
/* Baton helpers                                                       */
/* ------------------------------------------------------------------ */
/** Park the calling native context until s_runner == me (or shutdown). */
static void park_until_runner(host_thread_t *me)
{
    native_mutex_lock(&s_lock);
    me->parked = true;
    while (s_runner != me) {
        native_cond_wait(&me->run_cv, &s_lock);
    }
    me->parked = false;
    native_mutex_unlock(&s_lock);
}

/** Acquire the baton, but only when s_runner names me; else re-park.
 *  Returns holding the baton once (s_depth == 1). */
static void baton_acquire(host_thread_t *me)
{
    for (;;) {
        native_mutex_lock(&s_baton);
        native_mutex_lock(&s_lock);
        if (s_runner == me) {
            native_mutex_unlock(&s_lock);
            s_depth = 1u;
            return;
        }
        native_mutex_unlock(&s_lock);
        native_mutex_unlock(&s_baton);
        park_until_runner(me);
    }
}

/** Name @p rec as the next runner (under s_lock) and wake it. */
static void hand_off_to(host_thread_t *rec)
{
    native_mutex_lock(&s_lock);
    s_runner = rec;
    if (rec != NULL) {
        native_cond_signal(&rec->run_cv);
    }
    native_mutex_unlock(&s_lock);
}

/* ------------------------------------------------------------------ */
/* Thread trampoline                                                   */
/* ------------------------------------------------------------------ */
#ifdef _WIN32
static DWORD WINAPI thread_trampoline(LPVOID p)
#else
static void *thread_trampoline(void *p)
#endif
{
    host_thread_t *me = p;

    t_self = me;
    park_until_runner(me);
    baton_acquire(me);                  /* holding once (depth 1)          */
    native_mutex_unlock(&s_baton);      /* thread bodies run at depth 0    */
    s_depth = 0u;

    me->sai->entry(me->sai->arg);
    /* Returned: exit through the kernel; this context parks forever. */
    _sai_thread_cleanup_and_exit();
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/* ------------------------------------------------------------------ */
/* port_* implementation                                               */
/* ------------------------------------------------------------------ */
void port_init(void)
{
    native_mutex_init(&s_lock);
    native_mutex_init(&s_baton);
    s_runner = NULL;
    s_depth = 0u;
    s_isr_pending = 0u;
    s_inited = true;
}

uint32_t port_lock(void)
{
    if (t_self == NULL) {
        /* Supervisor / pre-kernel native context: plain mutex section. */
        native_mutex_lock(&s_baton);
        return 1u;                      /* key: not a runner section      */
    }
    for (;;) {
        native_mutex_lock(&s_baton);
        native_mutex_lock(&s_lock);
        if (s_runner == t_self) {
            native_mutex_unlock(&s_lock);
            s_depth++;
            return 0u;
        }
        native_mutex_unlock(&s_lock);
        native_mutex_unlock(&s_baton);
        park_until_runner(t_self);
    }
}

void port_unlock(uint32_t key)
{
    if (key == 1u || t_self == NULL) {
        native_mutex_unlock(&s_baton);  /* supervisor section: plain exit */
        return;
    }
    /* Outermost runner unlock: deliver the deferred IRQ scheduling tail
     * and/or service the preemption hint set by wake paths. */
    if (s_depth == 1u && _sai_current != NULL &&
        (s_isr_pending != 0u || _sai_preempt_hint)) {
        s_isr_pending = 0u;
        _sai_preempt_hint = false;
        sai_thread_t *prev = _sai_current;
        sai_thread_t *next = _sai_schedule_pick();   /* may re-queue prev */
        if (next != NULL && next != prev && next != &_sai_idle_thread) {
            port_switch(prev, next);    /* full release + park inside    */
            return;                     /* resumes here when scheduled   */
        }
    }
    if (s_depth > 0u) {
        s_depth--;
        native_mutex_unlock(&s_baton);
    }
}

bool port_in_isr(void)
{
    return false;   /* host ISRs run inline under the baton, not preemptive */
}

uint32_t port_spin_lock(volatile uint32_t *slock)
{
    port_lock();
    *slock = 1u;
    return 0u;
}

uint32_t port_spin_unlock(volatile uint32_t *slock, uint32_t saved)
{
    (void)saved;
    *slock = 0u;
    port_unlock(0u);
    return 0u;
}

void *port_stack_init(void *stack_top, size_t stack_size,
                      sai_thread_entry_t entry, void *arg)
{
    (void)stack_top;
    (void)stack_size;
    (void)entry;
    (void)arg;
    return (void *)(uintptr_t)1;        /* not used by the host port */
}

void port_thread_ready(sai_thread_t *t)
{
    /* First ready transition of a thread: create its record and spawn the
     * native thread.  Idempotent. */
    if (t == NULL || t->arch != NULL) {
        return;
    }
    host_thread_t *rec = calloc(1, sizeof(*rec));
    if (rec == NULL) {
        return;                          /* kernel will run without it */
    }
    rec->sai = t;
    native_cond_init(&rec->run_cv);

    native_mutex_lock(&s_lock);
    rec->next = s_threads;
    s_threads = rec;
    native_mutex_unlock(&s_lock);

    t->arch = rec;
#ifdef _WIN32
    rec->handle = CreateThread(NULL, 0, thread_trampoline, rec, 0, NULL);
#else
    pthread_create(&rec->handle, NULL, thread_trampoline, rec);
#endif
    rec->started = true;
}

void port_switch(sai_thread_t *from, sai_thread_t *to)
{
    host_thread_t *me = (from != NULL) ? (host_thread_t *)from->arch : NULL;
    host_thread_t *t  = (to   != NULL) ? (host_thread_t *)to->arch   : NULL;

    if (t == me || to == from) {
        return;                          /* kept running: nothing to do */
    }
    /* Release the baton completely (all critical-section levels). */
    while (s_depth > 0u) {
        s_depth--;
        native_mutex_unlock(&s_baton);
    }

    if (to == &_sai_idle_thread || t == NULL) {
        /* Host idle: free the runner slot; the supervisor owns wall-clock
         * time and will hand the baton back when something is ready. */
        hand_off_to(NULL);
    } else if (me != NULL) {
        /* Strict hand-off to the scheduler's pick. */
        hand_off_to(t);
    } else {
        return;                          /* no native context to park  */
    }

    if (me != NULL) {
        park_until_runner(me);
        baton_acquire(me);               /* resume holding once        */
        native_mutex_unlock(&s_baton);   /* ...and return at depth 0   */
        s_depth = 0u;
    }
}

void port_schedule_from_isr(void)
{
    s_isr_pending++;                     /* tail runs at next port_unlock */
}

void port_start_first_thread(sai_thread_t *t)
{
    /* The calling native thread (main) becomes the main SAI thread record;
     * sai_kernel_start() already set _sai_current = t. */
    memset(&s_main_rec, 0, sizeof(s_main_rec));
    s_main_rec.sai = t;
    native_cond_init(&s_main_rec.run_cv);
    t->arch = &s_main_rec;

    native_mutex_lock(&s_lock);
    s_main_rec.next = s_threads;
    s_threads = &s_main_rec;
    s_runner = &s_main_rec;              /* we are the initial runner */
    native_mutex_unlock(&s_lock);

    /* Start the supervisor thread (ticks + reaping + hand-offs). */
    s_timer_run = true;
#ifdef _WIN32
    s_timer_thread = CreateThread(NULL, 0, tick_thread_entry, NULL, 0, NULL);
#else
    pthread_create(&s_timer_thread, NULL, tick_thread_entry, NULL);
#endif
    s_timer_thread_valid = true;

    /* Run the main thread body on this context, with no baton held. */
    t_self = &s_main_rec;
    s_depth = 0u;
    t->entry(t->arg);

    /* main_fn returned but port_main_returned() should have exited the
     * process; if we ever get here, park forever. */
    hand_off_to(NULL);
    park_until_runner(&s_main_rec);
}

void port_idle(void)
{
    native_sleep_ms(1);
}

void port_idle_wait_for_interrupt(void)
{
    native_sleep_ms(SAI_TICK_MS);
}

uint32_t port_cycle_count(void)
{
    return sai_tick_count();
}

/* Tick source: the supervisor thread advances the kernel tick, so the
 * hardware-timer hooks are no-ops on the host. */
void port_timer_setup(uint32_t period_ms)
{
    (void)period_ms;
}

void port_timer_oneshot(uint32_t ticks)
{
    (void)ticks;                         /* tickless idle not simulated here */
}

void port_idle_until_tick(void)
{
    native_sleep_ms(SAI_TICK_MS);
}

/* What the kernel does when the main thread's function returns. */
void port_main_returned(void)
{
    fflush(stdout);
    s_timer_run = false;                 /* stop the supervisor */
    exit(s_exit_code);                   /* runs atexit handlers */
}

/* ------------------------------------------------------------------ */
/* Supervisor thread: wall clock + scheduling tail                     */
/* ------------------------------------------------------------------ */
#ifdef _WIN32
DWORD WINAPI tick_thread_entry(LPVOID arg)
#else
void *tick_thread_entry(void *arg)
#endif
{
    (void)arg;
    while (s_timer_run) {
        native_sleep_ms(SAI_TICK_MS);

        uint32_t key = port_lock();      /* t_self == NULL: supervisor key */
        _sai_tick_handler();
        _sai_zombie_reap();

        /* If the current runner still actively executes (it has not parked
         * since its last dispatch), it cannot be preempted here -- handing
         * the baton to someone else would run two threads at once.  Leave
         * it alone; it evicts itself at its next kernel entry (lazy
         * eviction, the documented host limitation for CPU-bound loops). */
        sai_thread_t *cur = _sai_current;
        host_thread_t *act = NULL;
        if (cur != NULL && cur != &_sai_idle_thread) {
            act = (host_thread_t *)cur->arch;
        }
        bool can_pick = (act == NULL) || act->parked;

        sai_thread_t *next = NULL;
        if (can_pick) {
            next = _sai_schedule_pick();
        }
        host_thread_t *rec = NULL;
        if (next != NULL && next != &_sai_idle_thread) {
            rec = (host_thread_t *)next->arch;
        }
        port_unlock(key);

        if (can_pick) {
            hand_off_to(rec);            /* invite the pick (or nobody)   */
        }
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/* ------------------------------------------------------------------ */
/* Simulated interrupts                                                */
/* ------------------------------------------------------------------ */
void sai_host_isr_register(uint32_t irq, sai_host_isr_t fn, void *arg)
{
    if (irq >= SAI_HOST_MAX_ISRS) {
        return;
    }
    s_isrs[irq].fn = fn;
    s_isrs[irq].arg = arg;
}

void sai_host_raise_isr(uint32_t irq)
{
    if (irq >= SAI_HOST_MAX_ISRS) {
        return;
    }
    /* Run inline under the baton (recursive); the deferred reschedule is
     * delivered by the port_unlock() tail below. */
    uint32_t key = port_lock();
    if (s_isrs[irq].fn != NULL) {
        s_isrs[irq].fn(s_isrs[irq].arg);
    }
    s_isr_count_total++;
    s_isr_pending++;
    port_unlock(key);
}

uint32_t sai_host_isr_count(void)
{
    return s_isr_count_total;
}

/* ------------------------------------------------------------------ */
/* Shutdown / atexit                                                   */
/* ------------------------------------------------------------------ */
static void host_cleanup(void)
{
    if (!s_inited) {
        return;
    }
    sai_host_shutdown();
}

void sai_host_shutdown(void)
{
    s_timer_run = false;
    if (s_timer_thread_valid) {
#ifdef _WIN32
        WaitForSingleObject(s_timer_thread, 1000);
        CloseHandle(s_timer_thread);
#else
        pthread_join(s_timer_thread, NULL);
#endif
        s_timer_thread_valid = false;
    }
}

void sai_host_atexit(void)
{
    atexit(host_cleanup);
}

void sai_host_advance_tick(uint32_t ticks)
{
    for (uint32_t i = 0; i < ticks; i++) {
        uint32_t key = port_lock();
        _sai_tick_handler();
        port_schedule_from_isr();        /* tail: pick + possible switch */
        port_unlock(key);
    }
}

void sai_host_advance_tick_sync(uint32_t ticks)
{
    sai_host_advance_tick(ticks);
}
