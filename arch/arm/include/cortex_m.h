/**
 * @file cortex_m.h
 * @brief Shared declarations for the ARM Cortex-M ports (M0+/M4/M7).
 */
#ifndef SAI_ARCH_CORTEX_M_H
#define SAI_ARCH_CORTEX_M_H

#include <sai/types.h>
#include <sai/kernel.h>
#include <sai/config.h>

/* Crash-safe console (polled) provided by the board layer. */
void sai_printf_safe(const char *fmt, ...);

/* C-side fault dumps (fault.c). */
struct cm_frame;
void hard_fault_c(const struct cm_frame *f, uint32_t exc_lr);
void memmanage_c(const struct cm_frame *f);
void busfault_c(const struct cm_frame *f);
void usagefault_c(const struct cm_frame *f);

/* Tickless re-arm hook (port_svcall.c). */
void _sai_tickless_rearm(void);

/* Next timer/sleep deadline in ticks (0 = none): computed by the kernel. */
uint32_t _sai_next_deadline_ticks(void);

#endif /* SAI_ARCH_CORTEX_M_H */
