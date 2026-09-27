/**
 * @file arch/arm/m4/startup.c
 * @brief ARMv7-M startup stage: .data/.bss init, Reset_Handler, default IRQ.
 * (Fault handlers live in fault.c; context switch in context_switch.S.)
 */
#include "sai/kernel.h"
#include "sai/port.h"
#include "sai/log.h"

extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sidata;
extern uint32_t _sbss;
extern uint32_t _ebss;

extern int main(void);
extern void board_early_init(void);

void Reset_Handler(void)
{
    uint32_t *src = &_sidata;
    for (uint32_t *dst = &_sdata; dst < &_edata; ) {
        *dst++ = *src++;
    }
    for (uint32_t *dst = &_sbss; dst < &_ebss; ) {
        *dst++ = 0u;
    }

    board_early_init();
    (void)main();

    for (;;) {
    }
}

void Default_Handler(void)
{
    sai_panic("unexpected interrupt", __FILE__, __LINE__);
}
