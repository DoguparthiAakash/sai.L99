/**
 * @file kernel/trace.c
 * @brief Tracing: a static, lock-free event ring for scheduler/ISR/budget
 *        events — the substrate for timeline tracing, the introspection
 *        stream and deterministic replay.
 *
 * The ring is written from scheduler paths with interrupts disabled, so
 * emission must be lock-free and non-allocating. It is an overwrite ring:
 * producers write (cursor++), readers snapshot via sai_trace_read().
 */
#include "internal.h"
#include <sai/trace.h>
#include <sai/console.h>
#include <string.h>

#define TRACE_RING_SLOTS 512u   /* power of two */

typedef struct {
    uint32_t seq;        /* global monotonic sequence                   */
    uint8_t  type;       /* sai_trace_event_t                           */
    uint8_t  cpu;        /* future SMP use                              */
    uint32_t ts;         /* tick timestamp                              */
    uint32_t a, b, c;    /* event payload                               */
} trace_slot_t;

static trace_slot_t s_ring[TRACE_RING_SLOTS];
static volatile uint32_t s_head;    /* next write slot                 */
static volatile uint32_t s_count;   /* events held (<= SLOTS)          */
static uint32_t s_seq;

void _sai_trace_event(uint8_t type, uint32_t a, uint32_t b, uint32_t c)
{
#if CONFIG_SAI_TRACE
    uint32_t key = port_lock();
    trace_slot_t *slot = &s_ring[s_head];
    slot->seq  = ++s_seq;
    slot->type = type;
    slot->cpu  = 0u;
    slot->ts   = sai_tick_count();
    slot->a = a;
    slot->b = b;
    slot->c = c;
    s_head = (s_head + 1u) & (TRACE_RING_SLOTS - 1u);
    if (s_count < TRACE_RING_SLOTS) {
        s_count++;
    }
    port_unlock(key);
#else
    (void)type; (void)a; (void)b; (void)c;
#endif
}

uint32_t sai_trace_count(void)
{
#if CONFIG_SAI_TRACE
    uint32_t key = port_lock();
    uint32_t n = s_count;
    port_unlock(key);
    return n;
#else
    return 0u;
#endif
}

uint32_t sai_trace_read(sai_trace_event_rec_t *out, uint32_t max)
{
    if (out == NULL || max == 0u) {
        return 0u;
    }
#if CONFIG_SAI_TRACE
    uint32_t key = port_lock();
    uint32_t n = (s_count < max) ? s_count : max;
    uint32_t start = (s_head + TRACE_RING_SLOTS - n) & (TRACE_RING_SLOTS - 1u);
    for (uint32_t i = 0; i < n; i++) {
        const trace_slot_t *s = &s_ring[(start + i) & (TRACE_RING_SLOTS - 1u)];
        out[i].seq  = s->seq;
        out[i].type = s->type;
        out[i].ts   = s->ts;
        out[i].a = s->a;
        out[i].b = s->b;
        out[i].c = s->c;
    }
    port_unlock(key);
    return n;
#else
    return 0u;
#endif
}

void sai_trace_dump(void)
{
#if CONFIG_SAI_TRACE
    sai_trace_event_rec_t evs[32];
    uint32_t n = sai_trace_read(evs, 32);
    sai_printf("trace: %lu events\r\n", (unsigned long)n);
    for (uint32_t i = 0; i < n; i++) {
        static const char *const names[] = {
            "SWITCH", "ISR_ENTER", "ISR_EXIT", "BUDGET",
        };
        const char *name = (evs[i].type <= 3u) ? names[evs[i].type] : "?";
        sai_printf("  [%6lu] %-8s a=%lu b=%lu c=%lu\r\n",
                   (unsigned long)evs[i].ts, name,
                   (unsigned long)evs[i].a, (unsigned long)evs[i].b,
                   (unsigned long)evs[i].c);
    }
#else
    sai_printf("trace: disabled (CONFIG_SAI_TRACE)\r\n");
#endif
}
