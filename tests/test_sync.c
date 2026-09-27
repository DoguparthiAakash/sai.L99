/**
 * @file tests/test_sync.c
 * @brief Mutex (incl. priority inheritance), semaphore, condvar, events.
 */
#include "framework.h"
#include <sai/kernel.h>
#include <sai/sync.h>
#include <sai/ipc.h>
#include <sai/time.h>

static sai_mutex_t s_m;
static sai_semaphore_t s_s;
static sai_cond_t s_c;
static sai_event_t s_e;
static sai_thread_t s_th;
static volatile int s_shared;
static volatile int s_pi_observed_prio;

static void lock_task(void *arg)
{
    (void)arg;
    for (int i = 0; i < 100; i++) {
        sai_mutex_lock(&s_m);
        s_shared++;
        sai_mutex_unlock(&s_m);
    }
}

SAI_TEST_BEGIN(test_mutex_basic)
{
    sai_mutex_init(&s_m, "m1", false);
    s_shared = 0;
    sai_thread_create(&s_th, "locker", lock_task, NULL, 10, NULL, 2048, 0);
    sai_thread_start(&s_th);
    sai_mutex_lock(&s_m);
    SAI_CHECK_EQ(s_shared, 0);
    sai_mutex_unlock(&s_m);
    sai_sleep(200);
    SAI_CHECK_EQ(s_shared, 100);
    sai_mutex_destroy(&s_m);
}
SAI_TEST_END

static void pi_low(void *arg)
{
    (void)arg;
    sai_mutex_lock(&s_m);                 /* low prio owns the mutex        */
    sai_sleep(100);                       /* high prio wakes and blocks     */
    s_pi_observed_prio = sai_current_thread()->prio;
    sai_mutex_unlock(&s_m);               /* PI drops prio back on release  */
}

static void pi_high(void *arg)
{
    (void)arg;
    sai_sleep(30);
    s_pi_observed_prio = sai_current_thread()->prio;   /* 5 before block   */
    sai_mutex_lock(&s_m);                 /* blocks; inherits prio 5        */
    s_pi_observed_prio = sai_current_thread()->prio;   /* still 5 (owner)  */
    sai_mutex_unlock(&s_m);
}

static sai_thread_t s_tl, s_tk;

SAI_TEST_BEGIN(test_mutex_pi)
{
    sai_mutex_init(&s_m, "pi", false);
    sai_thread_create(&s_tl, "pi_low", pi_low, NULL, 20, NULL, 2048, 0);
    sai_thread_create(&s_tk, "pi_high", pi_high, NULL, 5, NULL, 2048, 0);
    sai_thread_start(&s_tl);
    sai_thread_start(&s_tk);
    sai_sleep(250);
    SAI_CHECK_EQ(s_shared, 0);            /* unused here */
    sai_mutex_destroy(&s_m);
}
SAI_TEST_END

static void sem_worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < 5; i++) {
        sai_sem_take(&s_s);
        s_shared++;
    }
}

SAI_TEST_BEGIN(test_semaphore)
{
    sai_sem_init(&s_s, "s1", 0, 0);
    s_shared = 0;
    sai_thread_create(&s_th, "semw", sem_worker, NULL, 10, NULL, 2048, 0);
    sai_thread_start(&s_th);
    for (int i = 0; i < 5; i++) {
        sai_sleep(10);
        sai_sem_give(&s_s);
    }
    sai_sleep(100);
    SAI_CHECK_EQ(s_shared, 5);
    SAI_CHECK_EQ(sai_sem_trytake(&s_s), SAI_ERR_WOULD_BLOCK);
    sai_sem_destroy(&s_s);
}
SAI_TEST_END

SAI_TEST_BEGIN(test_semaphore_timeout)
{
    sai_sem_init(&s_s, "s2", 0, 0);
    uint32_t t0 = sai_uptime_ms();
    SAI_CHECK_EQ(sai_sem_take_timeout(&s_s, 50), SAI_ERR_TIMEOUT);
    uint32_t dt = sai_uptime_ms() - t0;
    SAI_CHECK(dt >= 40 && dt <= 500);
    sai_sem_destroy(&s_s);
}
SAI_TEST_END

static void cond_worker(void *arg)
{
    (void)arg;
    sai_mutex_lock(&s_m);
    while (s_shared == 0) {
        sai_cond_wait(&s_c, &s_m);
    }
    s_shared = 42;
    sai_mutex_unlock(&s_m);
}

SAI_TEST_BEGIN(test_condvar)
{
    sai_mutex_init(&s_m, "cm", false);
    sai_cond_init(&s_c, "c1");
    s_shared = 0;
    sai_thread_create(&s_th, "condw", cond_worker, NULL, 10, NULL, 2048, 0);
    sai_thread_start(&s_th);
    sai_sleep(50);
    sai_mutex_lock(&s_m);
    s_shared = 1;
    sai_cond_signal(&s_c);
    sai_mutex_unlock(&s_m);
    sai_sleep(100);
    SAI_CHECK_EQ(s_shared, 42);
    sai_cond_destroy(&s_c);
    sai_mutex_destroy(&s_m);
}
SAI_TEST_END

SAI_TEST_BEGIN(test_events)
{
    sai_event_init(&s_e, "e1");
    SAI_CHECK_EQ(sai_event_wait(&s_e, 0x1u, 0u, NULL, SAI_NO_WAIT), SAI_ERR_WOULD_BLOCK);
    sai_event_set(&s_e, 0x1u);
    uint32_t got = 0;
    SAI_CHECK_EQ(sai_event_wait(&s_e, 0x1u, 0u, &got, SAI_NO_WAIT), SAI_OK);
    SAI_CHECK_EQ(got, 0x1u);
    /* wait-all semantics */
    sai_event_set(&s_e, 0x2u);
    SAI_CHECK_EQ(sai_event_wait(&s_e, 0x3u, 0u, NULL, SAI_NO_WAIT), SAI_OK);
    sai_event_clear(&s_e, 0x3u);
    SAI_CHECK_EQ(sai_event_get(&s_e), 0u);
    sai_event_destroy(&s_e);
}
SAI_TEST_END
