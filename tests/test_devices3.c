/**
 * @file tests/test_devices3.c
 * @brief Third-wave device tests: ADC (blocking + async + averaging),
 *        DMA (burst transfer + completion callback + busy/stop), and the
 *        virtual signal/sink devices.
 */
#include "framework.h"
#include <sai/devices2.h>
#include <sai/kernel.h>
#include <sai/ipc.h>
#include <sai/time.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* ADC                                                                 */
/* ------------------------------------------------------------------ */

SAI_TEST_BEGIN(test_adc_blocking_and_averaging)
{
    sai_device_t *adc = NULL;
    SAI_CHECK_EQ(sai_adc_create("adct", &adc), SAI_OK);
    SAI_CHECK_EQ(adc->type, SAI_DEVICE_ADC);

    /* Set a stable level, configure 8-sample averaging. */
    SAI_CHECK_EQ(sai_adc_sim_set_level(adc, 0, 2000u), SAI_OK);
    SAI_CHECK_EQ(sai_adc_configure(adc, 0, 1000u, 8u), SAI_OK);
    sai_sleep(10);                       /* let the sample timer run */

    uint16_t v = 0;
    SAI_CHECK_EQ(sai_adc_read(adc, 0, &v), SAI_OK);
    SAI_CHECK_EQ(v, 2000u);              /* prefill => exact level */

    /* Unconfigured channel rejected. */
    SAI_CHECK_EQ(sai_adc_read(adc, 3, &v), SAI_ERR_INVAL);
    /* Out-of-range level rejected. */
    SAI_CHECK_EQ(sai_adc_sim_set_level(adc, 0, 5000u), SAI_ERR_INVAL);
}
SAI_TEST_END

static volatile uint16_t s_async_value;
static volatile uint32_t s_async_calls;

static void adc_cb(sai_device_t *dev, uint32_t channel, uint16_t value, void *arg)
{
    (void)dev; (void)channel; (void)arg;
    s_async_value = value;
    s_async_calls++;
}

SAI_TEST_BEGIN(test_adc_async_completion)
{
    sai_device_t *adc = NULL;
    SAI_CHECK_EQ(sai_adc_create("adcu", &adc), SAI_OK);
    SAI_CHECK_EQ(sai_adc_sim_set_level(adc, 1, 1234u), SAI_OK);
    SAI_CHECK_EQ(sai_adc_configure(adc, 1, 1000u, 4u), SAI_OK);

    s_async_value = 0;
    s_async_calls = 0;
    SAI_CHECK_EQ(sai_adc_read_async(adc, 1, adc_cb, NULL), SAI_OK);

    bool pending = false;
    SAI_CHECK(sai_adc_sim_pending(adc, 1, &pending));
    SAI_CHECK(pending);

    uint32_t spins = 0;
    while (s_async_calls == 0u && spins++ < 500) {
        sai_sleep(2);
    }
    SAI_CHECK_EQ(s_async_calls, 1u);
    SAI_CHECK_EQ(s_async_value, 1234u);
}
SAI_TEST_END

/* ------------------------------------------------------------------ */
/* DMA                                                                 */
/* ------------------------------------------------------------------ */

static volatile uint32_t s_dma_done_len;
static volatile int      s_dma_status;

static void dma_cb(sai_device_t *dev, uint32_t channel, uint32_t len,
                   int status, void *arg)
{
    (void)dev; (void)channel; (void)arg;
    s_dma_done_len = len;
    s_dma_status = status;
}

SAI_TEST_BEGIN(test_dma_transfer_and_completion)
{
    sai_device_t *dma = NULL;
    SAI_CHECK_EQ(sai_dma_create("dmat", &dma), SAI_OK);
    SAI_CHECK_EQ(dma->type, SAI_DEVICE_DMA);
    SAI_CHECK_EQ(sai_dma_configure(dma, 0, 64u), SAI_OK);

    static uint8_t src[256];
    static uint8_t dst[256];
    for (uint32_t i = 0; i < sizeof(src); i++) {
        src[i] = (uint8_t)i;
    }
    memset(dst, 0, sizeof(dst));

    s_dma_done_len = 0;
    s_dma_status = -1;
    SAI_CHECK_EQ(sai_dma_start(dma, 0, dst, src, sizeof(src), dma_cb, NULL),
                 SAI_OK);
    /* Channel busy while active. */
    SAI_CHECK_EQ(sai_dma_start(dma, 0, dst, src, 16u, dma_cb, NULL),
                 SAI_ERR_BUSY);

    uint32_t spins = 0;
    while (s_dma_done_len == 0u && spins++ < 5000) {
        sai_sleep(2);
    }
    SAI_CHECK_EQ(s_dma_done_len, 256u);
    SAI_CHECK_EQ(s_dma_status, SAI_OK);
    SAI_CHECK(memcmp(src, dst, sizeof(src)) == 0);

    uint32_t done = 0;
    SAI_CHECK(sai_dma_progress(dma, 0, &done));
    SAI_CHECK_EQ(done, 256u);
}
SAI_TEST_END

SAI_TEST_BEGIN(test_dma_stop_aborts)
{
    sai_device_t *dma = NULL;
    SAI_CHECK_EQ(sai_dma_create("dmas", &dma), SAI_OK);
    SAI_CHECK_EQ(sai_dma_configure(dma, 1, 8u), SAI_OK);   /* slow: 8B/tick */

    static uint8_t src[128];
    static uint8_t dst[128];
    memset(src, 0xAB, sizeof(src));
    memset(dst, 0, sizeof(dst));

    s_dma_done_len = 0;
    SAI_CHECK_EQ(sai_dma_start(dma, 1, dst, src, sizeof(src), dma_cb, NULL),
                 SAI_OK);
    sai_sleep(5);
    SAI_CHECK_EQ(sai_dma_stop(dma, 1), SAI_OK);

    /* Partial transfer only; callback never fired (completion < len). */
    uint32_t done = 0;
    SAI_CHECK(sai_dma_progress(dma, 1, &done));
    SAI_CHECK(done < 128u);
    SAI_CHECK_EQ(s_dma_done_len, 0u);
    SAI_CHECK(sai_dma_stop(dma, 9) == SAI_ERR_INVAL);
}
SAI_TEST_END

/* ------------------------------------------------------------------ */
/* Signal device                                                       */
/* ------------------------------------------------------------------ */

static volatile uint32_t s_sig_raised;

static void sig_cb(sai_device_t *dev, uint32_t flags, void *arg)
{
    (void)dev; (void)arg;
    s_sig_raised = flags;
}

SAI_TEST_BEGIN(test_signal_raise_wait)
{
    sai_device_t *sig = NULL;
    SAI_CHECK_EQ(sai_signal_create("sig1", &sig), SAI_OK);

    s_sig_raised = 0;
    SAI_CHECK_EQ(sai_signal_subscribe(sig, sig_cb, NULL), SAI_OK);

    /* Wait-side first (already-raised flags satisfy immediately). */
    SAI_CHECK_EQ(sai_signal_raise(sig, 0x4u), SAI_OK);
    SAI_CHECK_EQ(s_sig_raised, 0x4u);

    uint32_t got = 0;
    SAI_CHECK_EQ(sai_signal_wait(sig, 0x4u, SAI_EVENT_WAIT_ANY | SAI_EVENT_CONSUME,
                                 &got, 100), SAI_OK);
    SAI_CHECK_EQ(got, 0x4u);

    /* Unsubscribed pool: subscribing 5 times overflows at 4. */
    sai_device_t *sig2 = NULL;
    SAI_CHECK_EQ(sai_signal_create("sig2", &sig2), SAI_OK);
    for (int i = 0; i < 4; i++) {
        SAI_CHECK_EQ(sai_signal_subscribe(sig2, sig_cb, NULL), SAI_OK);
    }
    SAI_CHECK_EQ(sai_signal_subscribe(sig2, sig_cb, NULL), SAI_ERR_FULL);
}
SAI_TEST_END

/* ------------------------------------------------------------------ */
/* Sink device                                                         */
/* ------------------------------------------------------------------ */

SAI_TEST_BEGIN(test_sink_capture)
{
    sai_device_t *sink = NULL;
    SAI_CHECK_EQ(sai_sink_create("snk1", &sink), SAI_OK);

    const uint8_t msg[] = "hello sink";
    SAI_CHECK_EQ(sink->type, SAI_DEVICE_TYPE_SINK);
    /* Write through the class ops (sink member). */
    SAI_CHECK_EQ(sink->ops->sink.write(sink, msg, 10), 10);
    SAI_CHECK_EQ(sai_sink_written(sink), 10u);

    uint8_t buf[16] = { 0 };
    SAI_CHECK_EQ(sai_sink_read(sink, buf, sizeof(buf)), 10);
    SAI_CHECK(memcmp(buf, msg, 10) == 0);
}
SAI_TEST_END

SAI_TEST_BEGIN(test_new_devices_in_enumerator)
{
    bool saw_adc = false;
    bool saw_dma = false;
    bool saw_signal = false;
    bool saw_sink = false;
    sai_device_t *it = NULL;
    while ((it = sai_device_iter(it)) != NULL) {
        if (it->type == SAI_DEVICE_ADC)    saw_adc = true;
        if (it->type == SAI_DEVICE_DMA)    saw_dma = true;
        if (it->type == SAI_DEVICE_TYPE_SIGNAL) saw_signal = true;
        if (it->type == SAI_DEVICE_TYPE_SINK)   saw_sink = true;
    }
    SAI_CHECK(saw_adc);
    SAI_CHECK(saw_dma);
    SAI_CHECK(saw_signal);
    SAI_CHECK(saw_sink);
}
SAI_TEST_END
