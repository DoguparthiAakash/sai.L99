/**
 * @file arch/arm/m4/fault.c
 * @brief Fault handlers with stacked-register diagnostics (ARMv7-M).
 */
#include "cortex_m.h"
#include "sai/kernel.h"
#include "sai/port.h"
#include "sai/log.h"

struct cm_frame {
    uint32_t r0, r1, r2, r3;
    uint32_t r12, lr, pc, xpsr;
};

static void fault_dump(const char *name, const struct cm_frame *f, uint32_t cfsr)
{
    sai_printf_safe("\n*** FAULT: %s ***\n", name);
    if (f != NULL) {
        sai_printf_safe(" r0=%08x r1=%08x r2=%08x r3=%08x\n",
                        f->r0, f->r1, f->r2, f->r3);
        sai_printf_safe(" r12=%08x lr=%08x pc=%08x xpsr=%08x\n",
                        f->r12, f->lr, f->pc, f->xpsr);
    }
    sai_printf_safe(" cfsr=%08x hfsr=%08x dfsr=%08x afsr=%08x mmfar=%08x bfar=%08x\n",
                    cfsr,
                    *(volatile uint32_t *)0xE000ED2Cu,
                    *(volatile uint32_t *)0xE000ED30u,
                    *(volatile uint32_t *)0xE000ED3Cu,
                    *(volatile uint32_t *)0xE000ED28u,
                    *(volatile uint32_t *)0xE000ED38u);
    port_backtrace();
}

void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);

void HardFault_Handler(void)
{
    __asm__ volatile (
        "tst lr, #4                    \n"
        "ite eq                        \n"
        "mrseq r0, msp                 \n"
        "mrsne r0, psp                 \n"
        "mov r1, lr                    \n"
        "b hard_fault_c                \n"
        ::: "memory"
    );
}

void hard_fault_c(const struct cm_frame *f, uint32_t exc_lr)
{
    (void)exc_lr;
    uint32_t cfsr = *(volatile uint32_t *)0xE000ED28u;
    fault_dump("hardfault", f, cfsr);
    sai_panic("hardfault", __FILE__, __LINE__);
}

void MemManage_Handler(void)
{
    __asm__ volatile (
        "tst lr, #4                    \n"
        "ite eq                        \n"
        "mrseq r0, msp                 \n"
        "mrsne r0, psp                 \n"
        "b memmanage_c                 \n"
        ::: "memory"
    );
}

void memmanage_c(const struct cm_frame *f)
{
    fault_dump("memmanage", f, *(volatile uint32_t *)0xE000ED28u);
    sai_panic("memmanage", __FILE__, __LINE__);
}

void BusFault_Handler(void)
{
    __asm__ volatile (
        "tst lr, #4                    \n"
        "ite eq                        \n"
        "mrseq r0, msp                 \n"
        "mrsne r0, psp                 \n"
        "b busfault_c                  \n"
        ::: "memory"
    );
}

void busfault_c(const struct cm_frame *f)
{
    fault_dump("busfault", f, *(volatile uint32_t *)0xE000ED28u);
    sai_panic("busfault", __FILE__, __LINE__);
}

void UsageFault_Handler(void)
{
    __asm__ volatile (
        "tst lr, #4                    \n"
        "ite eq                        \n"
        "mrseq r0, msp                 \n"
        "mrsne r0, psp                 \n"
        "b usagefault_c                \n"
        ::: "memory"
    );
}

void usagefault_c(const struct cm_frame *f)
{
    fault_dump("usagefault", f, *(volatile uint32_t *)0xE000ED28u);
    sai_panic("usagefault", __FILE__, __LINE__);
}
