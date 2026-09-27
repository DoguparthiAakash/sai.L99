/**
 * @file arch/arm/m4/port_svcall.c
 * @brief Tickless compensation glue for the Cortex-M4 port.
 *
 * Historical note: the first-thread entry used to be an SVC trampoline
 * (SVC_Handler switching MSP->PSP).  Since the single-switch-exception
 * rework, the very first dispatch happens in PendSV (port_start_first_thread()
 * pends it; the boot context has nothing to save), so the SVC entry path is
 * gone and the vector-table entry is a weak alias to Default_Handler.
 */
#include "cortex_m.h"
#include "sai/port.h"
#include "sai/kernel.h"
#include "sai/time.h"

/* ------------------------------------------------------------------ */
/* Tickless idle support                                               */
/* ------------------------------------------------------------------ */
#if CONFIG_SAI_TICKLESS

void _sai_tickless_rearm(void)
{
    /* Called from SysTick when running in one-shot mode: advance time and
     * program the next wake. On Cortex-M we use SysTick itself as the
     * one-shot; between ticks the core sleeps in port_idle_until_tick(). */
    uint32_t next = _sai_next_deadline_ticks();
    port_timer_oneshot(next == 0u ? 1u : next);
}

#endif /* CONFIG_SAI_TICKLESS */
