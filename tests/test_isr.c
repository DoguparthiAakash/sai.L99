/**
 * @file tests/test_isr.c
 * @brief ISR-simulation tests: bounded-latency ISR-to-thread signaling.
 */
#include "framework.h"
#include <sai/kernel.h>
#include <sai/ipc.h>
#include <sai/time.h>
#include <sai/host.h>

#define TEST_IRQ 5

static sai_thread_t s_waiter;
static sai_semaphore_t s_sem;
static sai_event_t s_ev;
static volatile uint32_t s_isr_fires;
static volatile int s_signaled;

/* simulated ISR: gives a semaphore (ISR-safe path) */
static void sem_isr(void *arg)
{
    (void)arg;
    s_isr_fires++;
    sai_isr_sem_give(&s_sem);
}

static void waiter_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (sai_sem_take(&s_sem) == SAI_OK) {
            s_signaled++;
        }
    }
}

SAI_TEST_BEGIN(test_isr_sem_wakeup)
{
    SAI_CHECK_EQ(sai_sem_init(&s_sem, "isrsem", 0, 0), SAI_OK);
    s_isr_fires = 0;
    s_signaled = 0;

    sai_thread_create(&s_waiter, "isrw", waiter_task, NULL, 8, NULL, 2048, 0);
    sai_thread_start(&s_waiter);

    sai_host_isr_register(TEST_IRQ, sem_isr, NULL);
    for (int i = 0; i < 20; i++) {
        sai_host_raise_isr(TEST_IRQ);      /* "interrupt" fires           */
        sai_sleep(2);                      /* let the tail reschedule run */
    }
    SAI_CHECK_EQ(s_isr_fires, 20u);
    SAI_CHECK_EQ(s_signaled, 20);          /* all deliveries observed     */
    SAI_CHECK(sai_host_isr_count() >= 20u);
}
SAI_TEST_END

static void event_isr(void *arg)
{
    (void)arg;
    s_isr_fires++;
    sai_isr_event_set(&s_ev, 0x2u);
}

SAI_TEST_BEGIN(test_isr_event_wakeup)
{
    SAI_CHECK_EQ(sai_event_init(&s_ev, "isrev"), SAI_OK);
    s_isr_fires = 0;

    sai_host_isr_register(TEST_IRQ + 1, event_isr, NULL);
    sai_host_raise_isr(TEST_IRQ + 1);
    uint32_t got = 0;
    SAI_CHECK_EQ(sai_event_wait(&s_ev, 0x2u, SAI_EVENT_CONSUME, &got, 100), SAI_OK);
    SAI_CHECK_EQ(got, 0x2u);
    sai_event_destroy(&s_ev);
}
SAI_TEST_END
