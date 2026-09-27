/**
 * @file arch/arm/m0plus/port_m0.c
 * @brief ARMv6-M (Cortex-M0/M0+) port deltas: no BASEPRI, no fault regs.
 *
 * Context switch / SysTick / PendSV / SVC semantics are identical to the
 * M4 port (see ../m4/); only the critical section and fault handling differ.
 */
#include "sai/port.h"
#include "sai/kernel.h"

static uint32_t s_nesting;

uint32_t port_lock(void)
{
    uint32_t primask;
    __asm__ volatile ("mrs %0, primask" : "=r"(primask));
    __asm__ volatile ("cpsid i" ::: "memory");
    s_nesting++;
    return primask;
}

void port_unlock(uint32_t key)
{
    if (--s_nesting == 0u) {
        if (key & 1u) {
            __asm__ volatile ("cpsie i" ::: "memory");
        }
    }
}

bool port_in_isr(void)
{
    uint32_t ipsr;
    __asm__ volatile ("mrs %0, ipsr" : "=r"(ipsr));
    return (ipsr & 0x1FFu) != 0u;
}

uint32_t port_spin_lock(volatile uint32_t *slock)
{
    (void)slock;
    return port_lock();
}

uint32_t port_spin_unlock(volatile uint32_t *slock, uint32_t saved)
{
    (void)slock;
    port_unlock(saved);
    return 0u;
}
