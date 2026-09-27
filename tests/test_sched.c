/**
 * @file tests/test_sched.c
 * @brief Scheduler tests: priority scheduling, sleep/wake, yield, coop mode.
 */
#include "framework.h"
#include <sai/kernel.h>
#include <sai/time.h>
#include <sai/host.h>

static volatile int s_flag;
static sai_thread_t s_t1, s_t2, s_t3;

static void low_task(void *arg)
{
    (void)arg;
    for (int i = 0; i < 3; i++) {
        s_flag++;
        sai_sleep(20);
    }
}

static void high_task(void *arg)
{
    (void)arg;
    s_flag += 100;
    sai_sleep(5);
}

SAI_TEST_BEGIN(test_priority_order)
{
    s_flag = 0;
    sai_thread_create(&s_t1, "high", high_task, NULL, 5, NULL, 2048, 0);
    sai_thread_create(&s_t2, "low", low_task, NULL, 20, NULL, 2048, 0);
    sai_thread_start(&s_t1);
    sai_thread_start(&s_t2);
    sai_sleep(120);
    SAI_CHECK_EQ(s_flag, 103);
}
SAI_TEST_END

/* see framework.c for the runner; SAI_TEST_END is defined there */
