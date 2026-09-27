/**
 * @file arch/arm/m0plus/startup_m0.c
 * @brief ARMv6-M startup, SysTick, PendSV and SVC handlers.
 */
#include "sai/kernel.h"
#include "sai/port.h"
#include "sai/time.h"

#define SYST_CSR    (*(volatile uint32_t *)0xE000E010u)
#define SYST_RVR    (*(volatile uint32_t *)0xE000E014u)
#define SYST_CVR    (*(volatile uint32_t *)0xE000E018u)
#define SCB_ICSR    (*(volatile uint32_t *)0xE000ED04u)

void port_timer_setup(uint32_t period_ms)
{
    uint64_t load64 = ((uint64_t)CONFIG_SAI_CPU_CLOCK_HZ / 1000u) * period_ms;
    if (load64 > 0xFFFFFFull) {
        load64 = 0xFFFFFFull;
    }
    SYST_RVR = (uint32_t)load64 - 1u;
    SYST_CVR = 0u;
    SYST_CSR = (1u << 2) | (1u << 1) | (1u << 0);
}

void port_timer_oneshot(uint32_t ticks)
{
    uint64_t load64 = ((uint64_t)CONFIG_SAI_CPU_CLOCK_HZ / 1000u) * ticks;
    if (load64 == 0u) { load64 = 1u; }
    if (load64 > 0xFFFFFFull) { load64 = 0xFFFFFFull; }
    SYST_CSR = 0u;
    SYST_RVR = (uint32_t)load64 - 1u;
    SYST_CVR = 0u;
    SYST_CSR = (1u << 2) | (1u << 1) | (1u << 0);
}

void port_schedule_from_isr(void)
{
    SCB_ICSR = (1u << 28);      /* pend PendSV */
}

void port_start_first_thread(sai_thread_t *t)
{
    (void)t;
    __asm__ volatile ("svc #0" ::: "memory");
    for (;;) {
    }
}

void port_idle_until_tick(void)
{
    __asm__ volatile ("dsb\n wfi" ::: "memory");
}

void port_idle(void) { port_idle_until_tick(); }
void port_idle_wait_for_interrupt(void) { port_idle_until_tick(); }

uint32_t port_cycle_count(void) { return sai_tick_count(); }

void port_putchar(char c) { (void)c; }
void port_backtrace(void) { }
void port_halt(void) { for (;;) { __asm__ volatile ("wfi"); } }

void SysTick_Handler(void)
{
    _sai_tick_handler();
}

__attribute__((naked)) void PendSV_Handler(void)
{
    __asm__ volatile (
        "ldr r0, =_sai_current     \n"
        "ldr r1, [r0]              \n"
        "cmp r1, #0                \n"
        "beq 1f                    \n"
        "mrs r2, psp               \n"
        "stmdb r2!, {r4-r7}        \n"
        "mov r3, lr                \n"
        "mov r3, r3                \n"     /* (v6-M: also save r8-r11 via regs) */
        "mov  r3, r8               \n"
        "mov  r12, r9              \n"
        "push {r3, r12}            \n"
        "mov  r3, r10              \n"
        "mov  r12, r11             \n"
        "push {r3, r12}            \n"
        "ldr  r3, =0x8             \n"
        "add  r2, r2, r3           \n"     /* discard stale scratch */
        "str  r2, [r1, #4]         \n"
        "1:                        \n"
        "bl _sai_schedule          \n"
        "ldr r0, =_sai_current     \n"
        "ldr r1, [r0]              \n"
        "ldr r2, [r1, #4]          \n"
        "ldmia r2!, {r4-r7}        \n"
        "mov  r8, r4               \n"
        "mov  r9, r5               \n"
        "mov  r10, r6              \n"
        "mov  r11, r7              \n"
        "ldmia r2!, {r4-r7}        \n"
        "msr psp, r2               \n"
        "bx lr                     \n"
        ::: "memory"
    );
}

__attribute__((naked)) void SVC_Handler(void)
{
    __asm__ volatile (
        "ldr r0, =_sai_current     \n"
        "ldr r1, [r0]              \n"
        "ldr r2, [r1, #4]          \n"
        "msr psp, r2               \n"
        "mrs r3, control           \n"
        "orr r3, r3, #2            \n"
        "msr control, r3           \n"
        "isb                       \n"
        "bx lr                     \n"
        ::: "memory"
    );
}
