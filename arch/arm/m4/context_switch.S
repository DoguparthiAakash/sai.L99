/*
 * arch/arm/m4/context_switch.S
 * ARMv7-M (Cortex-M3/M4/M7) context switch and thread-exit trampoline.
 *
 * Switch model -- the port has ONE switch primitive, PendSV:
 *
 *   port_switch(from, to) is called from thread mode with interrupts
 *   already disabled (kernel critical section held across the call, see
 *   _sai_schedule() in kernel/sched.c).  It stores 'to' as the pending
 *   target, pends PendSV, and spins until PendSV has switched away from
 *   'from'.  PendSV:
 *     - saves the current PSP frame (r4-r11 + PSP) into _sai_current->sp,
 *     - loads to->sp into PSP and pops r4-r11,
 *     - marks the rendezvous word (the old thread's port_switch loop
 *       observes this and releases its port lock),
 *     - exception-returns into the new thread with BASEPRI open.
 *
 * The very first entry into a freshly built thread frame happens through
 * the same exception-return path in PendSV, so an initial frame shaped by
 * port_stack_init() (xPSR/PC/LR/R12/R3-R0 + r4-r11) is transparent.
 *
 * SAI_TCB_SP_OFFSET must match the C layout; pinned by _Static_assert in
 * arch/arm/m4/port.c.
 */
#include "tcb_offset.h"

    .syntax unified
    .thumb

    .set    TCB_ARCH,   SAI_TCB_SP_OFFSET + 4   /* sai_thread_t.arch      */

    .section .text.port_switch, "ax"
    .align  2
    .global port_switch
    .thumb_func
    .type   port_switch, %function
port_switch:
    /* r0 = from, r1 = to.  Called with a kernel critical section held. */
    str     r1, [r0, #TCB_ARCH]     @ from->arch = to (pending target)
    dsb
    ldr     r2, =0xE000ED04         @ SCB->ICSR
    ldr     r3, =0x10000000         @ PENDSVSET
    str     r3, [r2]
    dsb
    /* PendSV runs below the BASEPRI threshold: open a window so it can
     * fire.  The lock is effectively released by the switch itself (PendSV
     * zeroes the critical-section counter), so we return with interrupts
     * open -- matching the host baton port's post-switch depth of 0.
     * (v7-M always has BASEPRI; the port requires USE_BASEPRI, asserted
     * in arch/arm/m4/port.c.) */
1:  movs    r3, #0
    msr     basepri, r3
    ldr     r3, [r0, #TCB_ARCH]     @ wait until PendSV switched away
    cmp     r3, #0
    bne     1b
    bx      lr

    .section .text.sai_thread_exit_trampoline, "ax"
    .align  2
    .global sai_thread_exit_trampoline
    .thumb_func
    .type   sai_thread_exit_trampoline, %function
sai_thread_exit_trampoline:
    bl      sai_thread_exit         @ never returns
1:  b       1b
