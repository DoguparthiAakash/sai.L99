/**
 * @file tests/test_kernel.c
 * @brief Kernel-level tests: sleep/wake, timers, device registry, stack use.
 */
#include "framework.h"
#include <sai/kernel.h>
#include <sai/time.h>
#include <sai/device.h>
#include <sai/host.h>

static sai_thread_t s_wakee;
static volatile int s_woken;

static void sleep_task(void *arg)
{
    (void)arg;
    sai_sleep(2000);                       /* would sleep 2 s */
    s_woken = 1;
}

SAI_TEST_BEGIN(test_wake_before_deadline)
{
    s_woken = 0;
    sai_thread_create(&s_wakee, "wakee", sleep_task, NULL, 10, NULL, 2048, 0);
    sai_thread_start(&s_wakee);
    sai_sleep(50);
    SAI_CHECK_EQ(s_woken, 0);
    SAI_CHECK_EQ(sai_thread_wake(&s_wakee), SAI_OK);   /* early wake */
    sai_sleep(30);
    SAI_CHECK_EQ(s_woken, 1);
    sai_thread_join(&s_wakee, 1000);
}
SAI_TEST_END

static volatile int s_timer_fired;
static sai_timer_t s_timer;

static void timer_cb(void *arg)
{
    (void)arg;
    s_timer_fired++;
}

SAI_TEST_BEGIN(test_timer_periodic)
{
    s_timer_fired = 0;
    SAI_CHECK_EQ(sai_timer_init(&s_timer, "t1", timer_cb, NULL, 20, true), SAI_OK);
    SAI_CHECK_EQ(sai_timer_start(&s_timer), SAI_OK);
    sai_sleep(150);
    SAI_CHECK(s_timer_fired >= 5);         /* ~7 fires in 150 ms */
    SAI_CHECK(s_timer_fired <= 10);        /* but not too eager  */
    SAI_CHECK_EQ(sai_timer_stop(&s_timer), SAI_OK);
    uint32_t snapshot = s_timer_fired;
    sai_sleep(60);
    SAI_CHECK_EQ(s_timer_fired, snapshot); /* stopped for real */
    SAI_CHECK_EQ(sai_timer_is_active(&s_timer), false);
}
SAI_TEST_END

static int gpio_config_calls;
static int gpio_last_value;

static int dummy_cfg(sai_device_t *d, uint32_t pin, uint32_t f) { (void)d; (void)pin; (void)f; return 0; }
static int dummy_set(sai_device_t *d, uint32_t pin, bool v) { (void)d; (void)pin; gpio_last_value = v ? 1 : 0; return 0; }
static int dummy_get(sai_device_t *d, uint32_t pin, bool *v) { (void)d; (void)pin; *v = gpio_last_value != 0; return 0; }
static int dummy_toggle(sai_device_t *d, uint32_t pin) { (void)d; (void)pin; gpio_last_value ^= 1; return 0; }

SAI_TEST_BEGIN(test_device_registry)
{
    static const sai_dev_ops_t ops = {
        .gpio = { dummy_cfg, dummy_set, dummy_get, dummy_toggle },
    };
    static sai_device_t dev;
    SAI_CHECK_EQ(sai_device_register(&dev, "testled", SAI_DEVICE_GPIO,
                                     0x777u, NULL, 0u, &ops, NULL), SAI_OK);
    SAI_CHECK_EQ(sai_device_count() >= 1u, 1);
    sai_device_t *found = sai_device_get_by_node(0x777u);
    SAI_CHECK(found == &dev);

    SAI_CHECK_EQ(sai_gpio_configure(found, 12, SAI_GPIO_OUTPUT), 0);
    SAI_CHECK_EQ(sai_gpio_set(found, 12, true), 0);
    SAI_CHECK_EQ(gpio_last_value, 1);
    SAI_CHECK_EQ(sai_gpio_toggle(found, 12), 0);
    SAI_CHECK_EQ(gpio_last_value, 0);
    bool v = false;
    SAI_CHECK_EQ(sai_gpio_get(found, 12, &v), 0);
    SAI_CHECK_EQ(v, false);
}
SAI_TEST_END

SAI_TEST_BEGIN(test_timeslice_config)
{
    sai_sched_set_timeslice(7);
    SAI_CHECK_EQ(sai_sched_get_timeslice(), 7u);
    sai_sched_set_timeslice(10);

    sai_sched_set_mode(SAI_SCHED_COOPERATIVE);
    SAI_CHECK_EQ(sai_sched_get_mode(), SAI_SCHED_COOPERATIVE);
    sai_sched_set_mode(SAI_SCHED_PREEMPTIVE);
    SAI_CHECK_EQ(sai_sched_get_mode(), SAI_SCHED_PREEMPTIVE);
}
SAI_TEST_END
