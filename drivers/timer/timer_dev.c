/**
 * @file drivers/timer/timer_dev.c
 * @brief Timer device class helper: converts the raw class API into
 *        one-shot/periodic callbacks via kernel timers.
 */
#include "sai/device.h"
#include "sai/time.h"
#include "sai/log.h"

typedef struct timer_dev_ctx {
    sai_device_t *dev;
    sai_timer_t   ktimer;
    sai_timer_fn_t user_fn;
    void         *user_arg;
    bool          inited;
} timer_dev_ctx_t;

static timer_dev_ctx_t s_ctx[2];

static void timer_dev_trampoline(void *arg)
{
    timer_dev_ctx_t *ctx = arg;
    if (ctx->user_fn != NULL) {
        ctx->user_fn(ctx->user_arg);
    }
}

sai_status_t sai_timer_device_open(sai_device_t *dev, uint32_t idx,
                                   sai_timer_fn_t fn, void *arg)
{
    if (dev == NULL || idx >= 2u) {
        return SAI_ERR_INVAL;
    }
    timer_dev_ctx_t *ctx = &s_ctx[idx];
    ctx->dev = dev;
    ctx->user_fn = fn;
    ctx->user_arg = arg;
    sai_status_t rc = sai_timer_init(&ctx->ktimer, "timerdev",
                                     timer_dev_trampoline, ctx, 100u, true);
    if (rc != SAI_OK) {
        return rc;
    }
    ctx->inited = true;
    return SAI_OK;
}

sai_status_t sai_timer_device_start(sai_device_t *dev, uint32_t period_ms, bool periodic)
{
    (void)dev;
    for (uint32_t i = 0; i < 2u; i++) {
        if (s_ctx[i].inited && s_ctx[i].dev == dev) {
            s_ctx[i].ktimer.periodic = periodic ? 1u : 0u;
            return sai_timer_restart(&s_ctx[i].ktimer, period_ms);
        }
    }
    return SAI_ERR_NOENT;
}

sai_status_t sai_timer_device_stop(sai_device_t *dev)
{
    for (uint32_t i = 0; i < 2u; i++) {
        if (s_ctx[i].inited && s_ctx[i].dev == dev) {
            return sai_timer_stop(&s_ctx[i].ktimer);
        }
    }
    return SAI_ERR_NOENT;
}
