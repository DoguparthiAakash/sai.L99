/**
 * @file boards/stm32f407g-disc1/vector_table.c
 * @brief Cortex-M vector table for STM32F407.
 *
 * First 16 entries are the ARM core exceptions; entries 16..81 are the
 * STM32F4 IRQs. Unhandled IRQs point at Default_Handler.
 */
#include <stdint.h>

/* Initial stack pointer comes from the linker script (end of SRAM). */
extern uint32_t _estack;
extern void Reset_Handler(void);
extern void Default_Handler(void);

/* core exceptions */
extern void NMI_Handler(void);
extern void HardFault_Handler(void);
extern void MemManage_Handler(void);
extern void BusFault_Handler(void);
extern void UsageFault_Handler(void);
extern void DebugMon_Handler(void);
extern void PendSV_Handler(void);
extern void SysTick_Handler(void);

/* device IRQs used by this board; drivers override the weak default */
__attribute__((weak)) void USART2_IRQHandler(void) { Default_Handler(); }

/* First-thread entry now happens through PendSV (single-switch model), so
 * SVCall has no handler anymore. */
__attribute__((weak)) void SVC_Handler(void) { Default_Handler(); }

__attribute__((section(".isr_vector"), used))
void (*const g_vector_table[])(void) = {
    (void (*)(void))(&_estack),    /* 0: initial SP        */
    Reset_Handler,                 /* 1: reset             */
    NMI_Handler,                   /* 2                    */
    HardFault_Handler,             /* 3                    */
    MemManage_Handler,             /* 4                    */
    BusFault_Handler,              /* 5                    */
    UsageFault_Handler,            /* 6                    */
    0, 0, 0, 0,                    /* 7-10 reserved        */
    SVC_Handler,                   /* 11: SVCall           */
    DebugMon_Handler,              /* 12                   */
    0,                             /* 13 reserved          */
    PendSV_Handler,                /* 14: PendSV           */
    SysTick_Handler,               /* 15: SysTick          */
    /* IRQ0..: peripheral interrupts */
    [16 + 38] = USART2_IRQHandler, /* USART2_IRQn = 38     */
};

/* Weak core handlers can be overridden by the port; provide fallbacks. */
__attribute__((weak)) void NMI_Handler(void)        { Default_Handler(); }
__attribute__((weak)) void DebugMon_Handler(void)   { Default_Handler(); }
