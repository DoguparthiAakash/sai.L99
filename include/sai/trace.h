/**
 * @file sai/trace.h
 * @brief Scheduler/ISR event tracing: a static overwrite ring of timestamped
 *        events (context switches, ISR entry/exit, budget violations).
 *
 * Foundation for Tracealyzer-style timelines, the runtime introspection
 * stream and (later) deterministic record/replay. Gated by CONFIG_SAI_TRACE;
 * the API is always compiled so call sites stay link-clean.
 */
#ifndef SAI_TRACE_H
#define SAI_TRACE_H

#include <sai/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SAI_TRACE_SWITCH = 0,   /**< a=to-thread, b=from-thread, c=reason      */
    SAI_TRACE_ISR_ENTER,    /**< a=irq number                              */
    SAI_TRACE_ISR_EXIT,     /**< a=irq number                              */
    SAI_TRACE_BUDGET,       /**< a=thread, b=violations, c=action          */
} sai_trace_event_t;

typedef struct {
    uint32_t seq;    /**< monotonic sequence number                       */
    uint32_t ts;     /**< tick timestamp                                  */
    uint32_t a, b, c;
    uint8_t  type;
} sai_trace_event_rec_t;

/** Number of events currently held in the ring. */
uint32_t sai_trace_count(void);

/** Copy the newest @p max events (chronological) into @p out; returns count. */
uint32_t sai_trace_read(sai_trace_event_rec_t *out, uint32_t max);

/** Pretty-print the newest 32 events to the console. */
void sai_trace_dump(void);

/** Kernel-internal emitter (called with interrupts disabled). */
void _sai_trace_event(uint8_t type, uint32_t a, uint32_t b, uint32_t c);

#ifdef __cplusplus
}
#endif

#endif /* SAI_TRACE_H */
