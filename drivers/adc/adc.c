/**
 * @file drivers/adc/adc.c
 * @brief ADC class: software-simulated successive-approximation frontend
 *        with per-channel periodic sampling and hardware hook points.
 *
 * The simulator converts a programmable per-channel "input level" (0..4095)
 * through an N-sample moving average on a kernel timer, so tests are
 * deterministic. A real SoC replaces the ops table (the class API is
 * unchanged); the averaging + async machinery is reusable via the hooks
 * below.
 */
#include <sai/device.h>
#include <sai/time.h>
#include <sai/log.h>
#include <string.h>

#define SAI_ADC_INSTANCES 2
#define SAI_ADC_CHANNELS  4
#define SAI_ADC_MAX_AVG   16u

typedef struct {
    uint16_t level;          /**< simulated input level 0..4095          */
    uint16_t history[SAI_ADC_MAX_AVG];
    uint8_t  hist_len;       /**< samples averaged (1..16)               */
    uint8_t  hist_idx;
    uint32_t sample_us;
    sai_adc_cb_t cb;         /**< async completion (per channel)         */
    void    *cb_arg;
    uint8_t  async_pending;
} adc_channel_t;

typedef struct {
    sai_device_t  dev;
    char          name[8];
    adc_channel_t ch[SAI_ADC_CHANNELS];
    sai_timer_t   timer;
    bool          timer_on;
    volatile uint32_t conversions;
} adc_dev_t;

static adc_dev_t s_adcs[SAI_ADC_INSTANCES];

static adc_dev_t *adc_of(sai_device_t *d)
{
    for (uint32_t i = 0; i < SAI_ADC_INSTANCES; i++) {
        if (&s_adcs[i].dev == d) {
            return &s_adcs[i];
        }
    }
    return NULL;
}

/** Produce one (averaged) reading; the SoC hook replaces the sum. */
static uint16_t adc_convert(const adc_channel_t *ch)
{
    uint32_t sum = 0;
    uint8_t  n = ch->hist_len ? ch->hist_len : 1u;
    for (uint8_t i = 0; i < n; i++) {
        sum += ch->history[i];
    }
    return (uint16_t)(sum / n);
}

/** Kernel timer: sample every active channel; fire async callbacks. */
static void adc_tick(void *arg)
{
    adc_dev_t *a = (adc_dev_t *)arg;
    a->conversions++;
    for (uint32_t c = 0; c < SAI_ADC_CHANNELS; c++) {
        adc_channel_t *ch = &a->ch[c];
        if (ch->hist_len == 0u) {
            continue;                       /* channel not configured */
        }
        ch->history[ch->hist_idx] = ch->level;
        ch->hist_idx = (uint8_t)((ch->hist_idx + 1u) % ch->hist_len);
        if (ch->async_pending && ch->cb != NULL) {
            ch->async_pending = 0u;
            ch->cb(&a->dev, c, adc_convert(ch), ch->cb_arg);
        }
    }
}

static int adc_configure(sai_device_t *dev, uint32_t channel,
                         uint32_t sample_us, uint8_t samples)
{
    adc_dev_t *a = adc_of(dev);
    if (a == NULL || channel >= SAI_ADC_CHANNELS ||
        sample_us == 0u || samples == 0u || samples > SAI_ADC_MAX_AVG) {
        return SAI_ERR_INVAL;
    }
    adc_channel_t *ch = &a->ch[channel];
    ch->sample_us = sample_us;
    ch->hist_len = samples;
    ch->hist_idx = 0;
    for (uint8_t i = 0; i < samples; i++) {
        ch->history[i] = ch->level;         /* prefill: stable output */
    }
    if (!a->timer_on) {
        /* Kernel timer paced by the fastest configured channel. */
        uint32_t ms = (sample_us / 1000u) + 1u;
        (void)sai_timer_init(&a->timer, a->name, adc_tick, a, ms, true);
        (void)sai_timer_start(&a->timer);
        a->timer_on = true;
    }
    return SAI_OK;
}

static int adc_read(sai_device_t *dev, uint32_t channel, uint16_t *out)
{
    adc_dev_t *a = adc_of(dev);
    if (a == NULL || out == NULL || channel >= SAI_ADC_CHANNELS ||
        a->ch[channel].hist_len == 0u) {
        return SAI_ERR_INVAL;
    }
    *out = adc_convert(&a->ch[channel]);
    a->conversions++;
    return SAI_OK;
}

static int adc_read_async(sai_device_t *dev, uint32_t channel,
                          sai_adc_cb_t cb, void *arg)
{
    adc_dev_t *a = adc_of(dev);
    if (a == NULL || cb == NULL || channel >= SAI_ADC_CHANNELS ||
        a->ch[channel].hist_len == 0u) {
        return SAI_ERR_INVAL;
    }
    adc_channel_t *ch = &a->ch[channel];
    ch->cb = cb;
    ch->cb_arg = arg;
    ch->async_pending = 1u;
    return SAI_OK;
}

static const sai_dev_ops_t s_adc_ops = {
    .adc = {
        .configure = adc_configure,
        .read      = adc_read,
        .read_async = adc_read_async,
    },
};

sai_status_t sai_adc_create(const char *name, sai_device_t **out)
{
    if (name == NULL || out == NULL) {
        return SAI_ERR_INVAL;
    }
    adc_dev_t *a = NULL;
    for (uint32_t i = 0; i < SAI_ADC_INSTANCES; i++) {
        if (s_adcs[i].dev.type == SAI_DEVICE_UNKNOWN) {
            a = &s_adcs[i];
            break;
        }
    }
    if (a == NULL) {
        return SAI_ERR_FULL;
    }
    memset(a, 0, sizeof(*a));
    strncpy(a->name, name, sizeof(a->name) - 1u);
    sai_status_t rc = sai_device_register(&a->dev, a->name, SAI_DEVICE_ADC,
                                          0, NULL, 0, &s_adc_ops, NULL);
    if (rc != SAI_OK) {
        return rc;
    }
    *out = &a->dev;
    return SAI_OK;
}

/** Set the simulated input level of a channel (tests / board glue). */
sai_status_t sai_adc_sim_set_level(sai_device_t *d, uint32_t channel,
                                   uint16_t level)
{
    adc_dev_t *a = adc_of(d);
    if (a == NULL || channel >= SAI_ADC_CHANNELS || level > 4095u) {
        return SAI_ERR_INVAL;
    }
    a->ch[channel].level = level;
    return SAI_OK;
}

/** True when the last async read on @p channel is still awaiting a tick. */
bool sai_adc_sim_pending(sai_device_t *d, uint32_t channel, bool *pending)
{
    adc_dev_t *a = adc_of(d);
    if (a == NULL || channel >= SAI_ADC_CHANNELS || pending == NULL) {
        return false;
    }
    *pending = a->ch[channel].async_pending != 0u;
    return true;
}
