/**
 * @file sai/sync.h
 * @brief Synchronization primitives: mutex, semaphore, condition variable, spinlock.
 */
#ifndef SAI_SYNC_H
#define SAI_SYNC_H

#include <sai/types.h>
#include <sai/config.h>
#include <sai/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Wait queue: ordered list of blocked threads. Embedded by value in every
 * synchronization primitive, so the definition is public even though only
 * the kernel manipulates it. Ordering is by wq_key (FIFO tie-break) unless
 * WQ_FIFO is set.
 */
typedef struct sai_waitqueue {
    struct sai_thread *head;   /**< Sorted by wq_key.                    */
    uint32_t flags;            /**< WQ_FIFO / WQ_PRIO / WQ_EVENT / ...   */
} sai_waitqueue_t;

/* ------------------------------------------------------------------ */
/* Mutex (with priority inheritance)                                   */
/* ------------------------------------------------------------------ */
/**
 * Non-recursive mutex with priority inheritance. Attempting to double-lock
 * with SAI_MUTEX_TRY detects the deadlock and returns SAI_ERR_DEADLOCK.
 */
typedef struct sai_mutex {
    sai_kobj_t        kobj;
    struct sai_thread *owner;      /**< Current owner (NULL = unlocked). */
    uint32_t           lock_count; /**< >1 only for recursive mode.      */
    uint8_t            recursive;  /**< Allow nested locking.            */
    struct sai_waitqueue *waiters; /**< Blocked threads.                 */
    struct sai_waitqueue  wq_store;  /**< Embedded waitqueue storage.    */
    struct sai_mutex  *next_owned; /**< Owner's chain of held mutexes.   */
} sai_mutex_t;

/** Static initializer for an (optionally recursive) mutex. */
#define SAI_MUTEX_INIT(rec) { { { 0 }, SAI_KOBJ_MUTEX, 0, "mutex" }, NULL, 0, (rec), NULL, NULL }

sai_status_t sai_mutex_init(sai_mutex_t *m, const char *name, bool recursive);
sai_status_t sai_mutex_lock(sai_mutex_t *m);                  /**< Wait forever.  */
sai_status_t sai_mutex_lock_timeout(sai_mutex_t *m, int32_t timeout_ms);
sai_status_t sai_mutex_trylock(sai_mutex_t *m);               /**< No wait.       */
sai_status_t sai_mutex_unlock(sai_mutex_t *m);
/** Current owner (NULL if unlocked). */
sai_thread_t *sai_mutex_owner(const sai_mutex_t *m);
/** Destroy a mutex (must be unlocked; releases kernel bookkeeping). */
sai_status_t sai_mutex_destroy(sai_mutex_t *m);

/* ------------------------------------------------------------------ */
/* Semaphore                                                           */
/* ------------------------------------------------------------------ */
typedef struct sai_semaphore {
    sai_kobj_t        kobj;
    int32_t           count;
    int32_t           max;         /**< <= 0 means unbounded.            */
    struct sai_waitqueue *waiters;
    struct sai_waitqueue  wq_store;  /**< Embedded waitqueue storage.    */
} sai_semaphore_t;

#define SAI_SEM_INIT(cnt) { { { 0 }, SAI_KOBJ_SEM, 0, "sem" }, (cnt), 0, NULL }

sai_status_t sai_sem_init(sai_semaphore_t *s, const char *name, uint32_t initial, uint32_t max);
sai_status_t sai_sem_take(sai_semaphore_t *s);                       /**< Wait forever. */
sai_status_t sai_sem_take_timeout(sai_semaphore_t *s, int32_t timeout_ms);
sai_status_t sai_sem_trytake(sai_semaphore_t *s);
sai_status_t sai_sem_give(sai_semaphore_t *s);
/** ISR-safe give: never blocks; SAI_ERR_FULL when the limit is reached. */
sai_status_t sai_isr_sem_give(sai_semaphore_t *s);
/** Current count (approximate: racy without locking). */
int32_t sai_sem_count(const sai_semaphore_t *s);
sai_status_t sai_sem_destroy(sai_semaphore_t *s);

/* ------------------------------------------------------------------ */
/* Condition variable                                                  */
/* ------------------------------------------------------------------ */
/**
 * Condition variable bound to a mutex (Mesa style). Callers must hold the
 * mutex when waiting or signaling.
 */
typedef struct sai_cond {
    sai_kobj_t        kobj;
    struct sai_waitqueue *waiters;
    struct sai_waitqueue  wq_store;  /**< Embedded waitqueue storage.    */
} sai_cond_t;

#define SAI_COND_INIT { { { 0 }, SAI_KOBJ_COND, 0, "cond" }, NULL }

sai_status_t sai_cond_init(sai_cond_t *c, const char *name);
/** Atomically release @p m and wait; re-acquire before returning. */
sai_status_t sai_cond_wait(sai_cond_t *c, sai_mutex_t *m);
/** Timed variant. @p timeout_ms < 0 = forever. */
sai_status_t sai_cond_wait_timeout(sai_cond_t *c, sai_mutex_t *m, int32_t timeout_ms);
/** Wake one waiter (call with the mutex held). */
sai_status_t sai_cond_signal(sai_cond_t *c);
/** Wake all waiters (call with the mutex held). */
sai_status_t sai_cond_broadcast(sai_cond_t *c);
sai_status_t sai_cond_destroy(sai_cond_t *c);

/* ------------------------------------------------------------------ */
/* Spinlock                                                            */
/* ------------------------------------------------------------------ */
/**
 * Interrupt-safe spinlock for short critical sections, including between
 * cores and between ISR/thread contexts on the same core. On single-core
 * Cortex-M this maps to BASEPRI masking; on the host it maps to a native
 * ticket lock. Never call blocking APIs while holding a spinlock.
 */
typedef struct sai_spinlock {
    sai_kobj_t  kobj;
    uint32_t    slock;         /**< Ticket word (arch-dependent format). */
    uint32_t    saved;         /**< Saved interrupt state of the owner.  */
    sai_thread_t *owner;       /**< Diagnostics.                         */
} sai_spinlock_t;

#define SAI_SPINLOCK_INIT { { { 0 }, SAI_KOBJ_SPINLOCK, 0, "spin" }, 0u, 0u, (sai_thread_t *)0 }

void sai_spin_init(sai_spinlock_t *l);
void sai_spin_lock(sai_spinlock_t *l);
void sai_spin_unlock(sai_spinlock_t *l);
bool sai_spin_is_locked(const sai_spinlock_t *l);

/* Internal helpers shared with the kernel core. */
struct sai_waitqueue *_sai_wq_create(bool fifo);
void _sai_wq_destroy(struct sai_waitqueue *wq);
sai_status_t _sai_wq_wait(struct sai_waitqueue *wq, int32_t timeout_ms, uint32_t key);
sai_status_t _sai_wq_wake_one(struct sai_waitqueue *wq, sai_status_t result);
uint32_t _sai_wq_wake_all(struct sai_waitqueue *wq, sai_status_t result);
uint32_t _sai_wq_count(const struct sai_waitqueue *wq);

#ifdef __cplusplus
}
#endif

#endif /* SAI_SYNC_H */
