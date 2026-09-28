/**
 * @file drivers/dma/dma.c
 * @brief DMA class: channelized transfer engine with burst pacing and
 *        ISR-context completion callbacks.
 *
 * The generic backend moves memory in bursts from the tick handler, so
 * transfers take measurable, deterministic time and completion is deferred
 * out of the caller's context exactly like real DMA. An SoC replaces the
 * ops table with peripheral DMA; the channel/arb/callback shape is shared.
 */
#include <sai/device.h>
#include <sai/time.h>
#include <sai/log.h>
#include <string.h>

#define SAI_DMA_INSTANCES 1
#define SAI_DMA_CHANNELS  4

typedef struct {
    void        *dst;
    const void *src;
    uint32_t    len;          /**< total bytes                            */
    uint32_t    done;         /**< bytes moved so far                     */
    uint32_t    burst;        /**< bytes per tick                         */
    sai_dma_cb_t cb;
    void       *cb_arg;
    uint8_t     active;
} dma_channel_t;

typedef struct {
    sai_device_t  dev;
    char          name[8];
    dma_channel_t ch[SAI_DMA_CHANNELS];
    sai_timer_t   timer;
    bool          timer_on;
    volatile uint32_t transfers_done;
} dma_dev_t;

static dma_dev_t s_dmas[SAI_DMA_INSTANCES];

static dma_dev_t *dma_of(sai_device_t *d)
{
    for (uint32_t i = 0; i < SAI_DMA_INSTANCES; i++) {
        if (&s_dmas[i].dev == d) {
            return &s_dmas[i];
        }
    }
    return NULL;
}

/** Tick context: advance every active channel by one burst. */
static void dma_tick(void *arg)
{
    dma_dev_t *dm = (dma_dev_t *)arg;
    for (uint32_t c = 0; c < SAI_DMA_CHANNELS; c++) {
        dma_channel_t *ch = &dm->ch[c];
        if (!ch->active) {
            continue;
        }
        uint32_t remain = ch->len - ch->done;
        uint32_t n = (remain < ch->burst) ? remain : ch->burst;
        if (n > 0u) {
            memcpy((uint8_t *)ch->dst + ch->done,
                   (const uint8_t *)ch->src + ch->done, n);
            ch->done += n;
        }
        if (ch->done >= ch->len) {
            ch->active = 0u;
            dm->transfers_done++;
            if (ch->cb != NULL) {
                ch->cb(&dm->dev, c, ch->len, SAI_OK, ch->cb_arg);
            }
        }
    }
}

static void dma_timer_ensure(dma_dev_t *dm)
{
    if (!dm->timer_on) {
        (void)sai_timer_init(&dm->timer, dm->name, dma_tick, dm, 1u, true);
        (void)sai_timer_start(&dm->timer);
        dm->timer_on = true;
    }
}

static int dma_configure(sai_device_t *dev, uint32_t channel, uint32_t burst_size)
{
    dma_dev_t *dm = dma_of(dev);
    if (dm == NULL || channel >= SAI_DMA_CHANNELS || burst_size == 0u) {
        return SAI_ERR_INVAL;
    }
    dm->ch[channel].burst = burst_size;
    return SAI_OK;
}

static int dma_start(sai_device_t *dev, uint32_t channel,
                     void *dst, const void *src, uint32_t len,
                     sai_dma_cb_t cb, void *arg)
{
    dma_dev_t *dm = dma_of(dev);
    if (dm == NULL || channel >= SAI_DMA_CHANNELS ||
        dst == NULL || src == NULL || len == 0u) {
        return SAI_ERR_INVAL;
    }
    dma_channel_t *ch = &dm->ch[channel];
    if (ch->active) {
        return SAI_ERR_BUSY;
    }
    ch->dst = dst;
    ch->src = src;
    ch->len = len;
    ch->done = 0u;
    ch->cb = cb;
    ch->cb_arg = arg;
    if (ch->burst == 0u) {
        ch->burst = 32u;                /* default arbitration quantum */
    }
    ch->active = 1u;
    dma_timer_ensure(dm);
    return SAI_OK;
}

static int dma_stop(sai_device_t *dev, uint32_t channel)
{
    dma_dev_t *dm = dma_of(dev);
    if (dm == NULL || channel >= SAI_DMA_CHANNELS) {
        return SAI_ERR_INVAL;
    }
    dm->ch[channel].active = 0u;
    return SAI_OK;
}

static const sai_dev_ops_t s_dma_ops = {
    .dma = {
        .configure = dma_configure,
        .start     = dma_start,
        .stop      = dma_stop,
    },
};

sai_status_t sai_dma_create(const char *name, sai_device_t **out)
{
    if (name == NULL || out == NULL) {
        return SAI_ERR_INVAL;
    }
    dma_dev_t *dm = &s_dmas[0];
    if (dm->dev.type != SAI_DEVICE_UNKNOWN) {
        return SAI_ERR_FULL;
    }
    memset(dm, 0, sizeof(*dm));
    strncpy(dm->name, name, sizeof(dm->name) - 1u);
    sai_status_t rc = sai_device_register(&dm->dev, dm->name, SAI_DEVICE_DMA,
                                          0, NULL, 0, &s_dma_ops, NULL);
    if (rc != SAI_OK) {
        return rc;
    }
    *out = &dm->dev;
    return SAI_OK;
}

/** Progress of a channel (bytes moved; only meaningful when inactive). */
bool sai_dma_progress(sai_device_t *d, uint32_t channel, uint32_t *done)
{
    dma_dev_t *dm = dma_of(d);
    if (dm == NULL || channel >= SAI_DMA_CHANNELS || done == NULL) {
        return false;
    }
    *done = dm->ch[channel].done;
    return true;
}
