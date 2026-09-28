/**
 * @file tests/test_devices2.c
 * @brief Second-wave device tests: PWM, buttons, loopback pair, mem device.
 */
#include "framework.h"
#include <sai/devices2.h>
#include <sai/kernel.h>
#include <sai/ipc.h>
#include <sai/time.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* PWM                                                                 */
/* ------------------------------------------------------------------ */

SAI_TEST_BEGIN(test_pwm_lifecycle)
{
    sai_device_t *pwm = NULL;
    SAI_CHECK_EQ(sai_pwm_create("pwmtest", &pwm), SAI_OK);
    SAI_CHECK_EQ(pwm->type, SAI_DEVICE_TYPE_PWM);
    SAI_CHECK_EQ(sai_pwm_configure(pwm, 20000u), SAI_OK);   /* 50 Hz */

    SAI_CHECK_EQ(sai_pwm_enable(pwm, 0, 1500u), SAI_OK);    /* servo mid */
    SAI_CHECK_EQ(sai_pwm_enable(pwm, 1, 1500u), SAI_OK);
    SAI_CHECK_EQ(sai_pwm_enable(pwm, 0, 99999u), SAI_ERR_INVAL); /* > period */
    SAI_CHECK_EQ(sai_pwm_disable(pwm, 0), SAI_OK);
    SAI_CHECK_EQ(sai_pwm_disable(pwm, 1), SAI_OK);

    /* out-of-range class calls rejected */
    SAI_CHECK_EQ(sai_pwm_enable(pwm, 4, 10u), SAI_ERR_INVAL);
}
SAI_TEST_END

/* ------------------------------------------------------------------ */
/* Buttons                                                             */
/* ------------------------------------------------------------------ */

static volatile uint32_t s_press_events;
static volatile uint32_t s_release_events;

static void btn_cb(sai_device_t *dev, uint32_t id, bool pressed, void *arg)
{
    (void)dev; (void)arg;
    if (pressed) {
        s_press_events++;
    } else {
        s_release_events++;
    }
}

SAI_TEST_BEGIN(test_button_debounce_and_events)
{
    s_press_events = 0;
    s_release_events = 0;

    sai_device_t *btn = NULL;
    SAI_CHECK_EQ(sai_button_create("btntest", &btn), SAI_OK);
    SAI_CHECK_EQ(sai_button_configure(btn, 20u), SAI_OK);
    SAI_CHECK_EQ(sai_button_subscribe(btn, btn_cb, NULL), SAI_OK);

    /* raw press -> after debounce (one-shot timer) the level commits */
    SAI_CHECK_EQ(sai_button_irq(btn, 0, true), SAI_OK);
    sai_sleep(60);
    bool pressed = false;
    SAI_CHECK_EQ(sai_button_read(btn, 0, &pressed), SAI_OK);
    SAI_CHECK(pressed);

    /* raw release */
    SAI_CHECK_EQ(sai_button_irq(btn, 0, false), SAI_OK);
    sai_sleep(60);
    SAI_CHECK_EQ(sai_button_read(btn, 0, &pressed), SAI_OK);
    SAI_CHECK(!pressed);

    SAI_CHECK(s_press_events >= 1u);
    SAI_CHECK(s_release_events >= 1u);

    /* id out of range */
    SAI_CHECK_EQ(sai_button_irq(btn, 4, true), SAI_ERR_INVAL);
}
SAI_TEST_END

/* ------------------------------------------------------------------ */
/* Loopback pair                                                       */
/* ------------------------------------------------------------------ */

SAI_TEST_BEGIN(test_loopback_pair)
{
    sai_device_t *a = NULL;
    sai_device_t *b = NULL;
    SAI_CHECK_EQ(sai_loopback_create("lba", "lbb", &a, &b), SAI_OK);
    SAI_CHECK(a != NULL && b != NULL && a != b);

    /* write on a, read on b */
    const uint8_t msg[] = "ping";
    SAI_CHECK_EQ(sai_loopback_write(a, msg, 4), 4);
    uint8_t buf[16];
    memset(buf, 0, sizeof(buf));
    SAI_CHECK_EQ(sai_loopback_read(b, buf, sizeof(buf), 100), 4);
    SAI_CHECK(memcmp(buf, msg, 4) == 0);

    /* write on b, read on a */
    const uint8_t msg2[] = "pong!";
    SAI_CHECK_EQ(sai_loopback_write(b, msg2, 5), 5);
    SAI_CHECK_EQ(sai_loopback_read(a, buf, sizeof(buf), 100), 5);
    SAI_CHECK(memcmp(buf, msg2, 5) == 0);

    /* timeout read with empty queue */
    SAI_CHECK_EQ(sai_loopback_read(b, buf, sizeof(buf), 10), 0);
}
SAI_TEST_END

/* ------------------------------------------------------------------ */
/* Memory device                                                       */
/* ------------------------------------------------------------------ */

SAI_TEST_BEGIN(test_memdev)
{
    static uint8_t store[64];
    sai_device_t *m = NULL;
    SAI_CHECK_EQ(sai_memdev_create("memtest", store, sizeof(store), &m), SAI_OK);
    SAI_CHECK_EQ(m->type, SAI_DEVICE_TYPE_MEM);

    const uint8_t data[] = { 1, 2, 3, 4, 5 };
    SAI_CHECK_EQ(sai_mem_write(m, 10, data, sizeof(data)), SAI_OK);
    uint8_t back[5] = { 0 };
    SAI_CHECK_EQ(sai_mem_read(m, 10, back, sizeof(back)), SAI_OK);
    SAI_CHECK(memcmp(back, data, sizeof(data)) == 0);

    /* bounds */
    SAI_CHECK_EQ(sai_mem_write(m, 60, data, sizeof(data)), SAI_ERR_BOUNDS);
    SAI_CHECK_EQ(sai_mem_read(m, 63, back, 2), SAI_ERR_BOUNDS);
}
SAI_TEST_END

/* ------------------------------------------------------------------ */
/* Device enumerator sees the new classes                              */
/* ------------------------------------------------------------------ */

SAI_TEST_BEGIN(test_device_iter_includes_new_classes)
{
    bool saw_pwm = false;
    bool saw_button = false;
    bool saw_loopback = false;
    bool saw_mem = false;
    sai_device_t *it = NULL;
    uint32_t count = 0;
    while ((it = sai_device_iter(it)) != NULL) {
        count++;
        if (it->type == SAI_DEVICE_TYPE_PWM)    saw_pwm = true;
        if (it->type == SAI_DEVICE_TYPE_BUTTON) saw_button = true;
        if (it->type == SAI_DEVICE_TYPE_LOOPBK) saw_loopback = true;
        if (it->type == SAI_DEVICE_TYPE_MEM)    saw_mem = true;
    }
    SAI_CHECK_EQ(count, sai_device_count());
    SAI_CHECK(saw_pwm);
    SAI_CHECK(saw_button);
    SAI_CHECK(saw_loopback);
    SAI_CHECK(saw_mem);
}
SAI_TEST_END
