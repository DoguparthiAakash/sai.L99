/**
 * @file drivers/pwm/pwm.c
 * @brief PWM class: software PWM on kernel timers (portable), with an ops
 *        hook for SoC hardware PWM drivers to override.
 *
 * Two instances are pooled ("pwm0", "pwm1"); each has 4 channels.  The
 * software engine flips GPIO-less state and records duty so tests can
 * observe it; boards with real timers replace the set_duty hook.
 */
#include <sai/devices2.h>
#include <sai/time.h>
#include <sai/log.h>
#include <string.h>

#define SAI_PWM_INSTANCES 2
#define SAI_PWM_CHANNELS  4

typedef struct {
    uint32_t duty_us;       /**< Active-level time within the period.       */
    bool     enabled;
} pwm_channel_t;

typedef struct {
    sai_device_t   dev;
    char           name[8];
    uint32_t       period_us;
    pwm_channel_t  ch[SAI_PWM_CHANNELS];
    sai_timer_t    timer;
    bool           timer_running;
    volatile uint32_t cycles;   /**< Completed periods (diagnostics).       */
} pwm_dev_t;

static pwm_dev_t s_pwm[SAI_PWM_INSTANCES];

static pwm_dev_t *pwm_of(sai_device_t *d)
{
    for (uint32_t i = 0; i < SAI_PWM_INSTANCES; i++) {
        if (&s_pwm[i].dev == d) {
            return &s_pwm[i];
        }
    }
    return NULL;
}

/** Tick the waveform: called by the kernel timer every period/2. */
static void pwm_timer_tick(void *arg)
{
    pwm_dev_t *p = (pwm_dev_t *)arg;
    /* Half-period bookkeeping; a real SoC driver would toggle hardware
     * compare outputs here.  The software model just counts cycles. */
    p->cycles++;
}

static int pwm_configure(sai_device_t *dev, uint32_t period_us)
{
    pwm_dev_t *p = pwm_of(dev);
    if (p == NULL || period_us == 0u) {
        return SAI_ERR_INVAL;
    }
    p->period_us = period_us;
    return SAI_OK;
}

static int pwm_enable(sai_device_t *dev, uint32_t ch, uint32_t duty_us)
{
    pwm_dev_t *p = pwm_of(dev);
    if (p == NULL || ch >= SAI_PWM_CHANNELS || p->period_us == 0u ||
        duty_us > p->period_us) {
        return SAI_ERR_INVAL;
    }
    if (!p->timer_running) {
        /* Kernel timer at half the period so both edges are modeled. */
        uint32_t half_ms = (p->period_us / 2000u) + 1u;
        (void)sai_timer_init(&p->timer, p->name, pwm_timer_tick, p,
                             half_ms, true);
        (void)sai_timer_start(&p->timer);
        p->timer_running = true;
    }
    p->ch[ch].duty_us = duty_us;
    p->ch[ch].enabled = true;
    return SAI_OK;
}

static int pwm_disable(sai_device_t *dev, uint32_t ch)
{
    pwm_dev_t *p = pwm_of(dev);
    if (p == NULL || ch >= SAI_PWM_CHANNELS) {
        return SAI_ERR_INVAL;
    }
    p->ch[ch].enabled = false;
    p->ch[ch].duty_us = 0u;
    bool any = false;
    for (uint32_t i = 0; i < SAI_PWM_CHANNELS; i++) {
        any = any || p->ch[i].enabled;
    }
    if (!any && p->timer_running) {
        (void)sai_timer_stop(&p->timer);
        p->timer_running = false;
    }
    return SAI_OK;
}

static const sai_pwm_ops_t s_pwm_ops = {
    .configure = pwm_configure,
    .enable    = pwm_enable,
    .disable   = pwm_disable,
};

sai_status_t sai_pwm_create(const char *name, sai_device_t **out)
{
    if (name == NULL || out == NULL) {
        return SAI_ERR_INVAL;
    }
    pwm_dev_t *p = NULL;
    for (uint32_t i = 0; i < SAI_PWM_INSTANCES; i++) {
        if (s_pwm[i].dev.type == SAI_DEVICE_UNKNOWN) {
            p = &s_pwm[i];
            break;
        }
    }
    if (p == NULL) {
        return SAI_ERR_FULL;
    }
    memset(p, 0, sizeof(*p));
    strncpy(p->name, name, sizeof(p->name) - 1u);
    sai_status_t rc = sai_device_register(&p->dev, p->name,
                                          SAI_DEVICE_TYPE_PWM, 0, NULL, 0,
                                          (const sai_dev_ops_t *)&s_pwm_ops,
                                          NULL);
    if (rc != SAI_OK) {
        return rc;
    }
    *out = &p->dev;
    return SAI_OK;
}
