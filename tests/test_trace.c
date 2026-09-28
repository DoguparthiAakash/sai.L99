/**
 * @file tests/test_trace.c
 * @brief Trace-ring tests: event capture on context switch, chronological
 *        read-out, and overwrite-ring behavior.
 */
#include "framework.h"
#include <sai/kernel.h>
#include <sai/trace.h>
#include <sai/ipc.h>

SAI_TEST_BEGIN(test_trace_basic_read)
{
    /* The scheduler emits SWITCH events during normal test operation; the
     * ring must hold a readable, chronologically ordered snapshot. */
    sai_trace_event_rec_t evs[16];
    uint32_t n = sai_trace_read(evs, 16);
    SAI_CHECK(n > 0u);
    SAI_CHECK(n <= 16u);
    SAI_CHECK_EQ(sai_trace_count(), n);
    for (uint32_t i = 1; i < n; i++) {
        SAI_CHECK(evs[i].seq > evs[i - 1].seq);   /* strictly ordered */
    }
}
SAI_TEST_END

SAI_TEST_BEGIN(test_trace_ring_overwrites)
{
    /* Flood the ring with synthetic events (well over 512) and verify the
     * read returns only the newest window. */
    for (uint32_t i = 0; i < 600u; i++) {
        _sai_trace_event((uint8_t)SAI_TRACE_ISR_ENTER, i, 0u, 0u);
    }
    SAI_CHECK_EQ(sai_trace_count(), 512u);

    sai_trace_event_rec_t evs[8];
    uint32_t n = sai_trace_read(evs, 8);
    SAI_CHECK_EQ(n, 8u);
    /* The last synthetic event id was 599. */
    SAI_CHECK_EQ(evs[7].a, 599u);
    for (uint32_t i = 1; i < n; i++) {
        SAI_CHECK(evs[i].a > evs[i - 1].a);
    }
}
SAI_TEST_END

SAI_TEST_BEGIN(test_trace_dump_smoke)
{
    sai_trace_dump();                    /* must not crash */
    SAI_CHECK(true);
}
SAI_TEST_END
