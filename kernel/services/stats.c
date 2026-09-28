/**
 * @file kernel/services/stats.c
 * @brief Stats service: coherent kernel diagnostics snapshot + heartbeat.
 */
#include "internal.h"
#include <sai/services.h>
#include <sai/console.h>
#include <sai/device.h>
#include <sai/time.h>
#include <sai/budget.h>

void sai_stats_get(sai_stats_snapshot_t *out)
{
    if (out == NULL) {
        return;
    }
    out->ticks           = sai_tick_count();
    out->ctx_switches    = _sai_ctx_switches;
    out->isr_reschedules = _sai_isr_reschedules;
    out->threads         = _sai_kobj_count(SAI_KOBJ_THREAD);
    out->kobjects        = 0;
    for (int t = SAI_KOBJ_THREAD; t <= SAI_KOBJ_DEVICE; t++) {
        out->kobjects += _sai_kobj_count((uint16_t)t);
    }
    out->devices       = sai_device_count();
    out->timers        = sai_timer_active_count();
    out->heap_free     = sai_mem_free_bytes();
    out->heap_max_free = sai_mem_max_free_block();
    out->scripts_run   = sai_script_runs();
    out->script_errors = sai_script_errors();
    out->budget_violations = sai_budget_violations_total();
}

void sai_stats_print(const sai_stats_snapshot_t *s)
{
    if (s == NULL) {
        return;
    }
    sai_printf("stats: up=%lums ctx=%lu isr=%lu threads=%lu kobj=%lu\r\n",
               (unsigned long)s->ticks, (unsigned long)s->ctx_switches,
               (unsigned long)s->isr_reschedules, (unsigned long)s->threads,
               (unsigned long)s->kobjects);
    sai_printf("stats: dev=%lu timers=%lu heap_free=%lu heap_max_blk=%lu "
               "scripts=%lu err=%lu\r\n",
               (unsigned long)s->devices, (unsigned long)s->timers,
               (unsigned long)s->heap_free,
               (unsigned long)s->heap_max_free,
               (unsigned long)s->scripts_run,
               (unsigned long)s->script_errors);
}

/* ------------------------------------------------------------------ */
/* Heartbeat daemon                                                    */
/* ------------------------------------------------------------------ */

static sai_service_t *s_statsd;
static volatile uint32_t s_interval_ms = 1000u;

static void statsd_loop(sai_service_t *svc)
{
    sai_stats_snapshot_t snap;
    uint32_t scratch;
    for (;;) {
        sai_status_t rc = sai_service_recv(svc, &scratch,
                                           (int32_t)s_interval_ms);
        if (rc == SAI_ERR_STATE) {
            return;                     /* stop requested */
        }
        if (rc == SAI_ERR_TIMEOUT) {
            sai_stats_get(&snap);
            SAI_LOGI("statsd", "up=%ums ctx=%u thr=%u heap=%uB",
                     (unsigned)snap.ticks, (unsigned)snap.ctx_switches,
                     (unsigned)snap.threads, (unsigned)snap.heap_free);
        }
    }
}

sai_status_t sai_stats_service_start(uint32_t interval_ms)
{
    if (interval_ms == 0u) {
        interval_ms = 1000u;
    }
    if (s_statsd != NULL && !sai_service_is_stopped(s_statsd)) {
        s_interval_ms = interval_ms;
        return SAI_OK;
    }
    s_interval_ms = interval_ms;
    sai_status_t rc = sai_service_start("statsd", statsd_loop, NULL, 2048, 20);
    if (rc != SAI_OK) {
        return rc;
    }
    s_statsd = sai_service_find("statsd");
    return (s_statsd != NULL) ? SAI_OK : SAI_ERR_NOENT;
}

sai_status_t sai_stats_service_stop(void)
{
    if (s_statsd == NULL) {
        return SAI_ERR_NOINIT;
    }
    sai_status_t rc = sai_service_stop(s_statsd);
    s_statsd = NULL;
    return rc;
}
