/**
 * @file arch/arm/m4/port.c
 * @brief ARMv7-M (Cortex-M3/M4/M7) port of the sai.L99 kernel.
 *
 * Interrupt model:
 *   - CONFIG_SAI_ARM_USE_BASEPRI=1 (default for M3/M4/M7): critical sections
 *     mask interrupts via BASEPRI (keeps fault handlers live); ISRs must run
 *     at a priority numerically >= CONFIG_SAI_BASEPRI_THRESHOLD.
 *   - Otherwise PRIMASK is used (mandatory on M0/M0+).
 *
 * Scheduling:
 *   - SysTick (or a 32-bit timer for tickless) drives _sai_tick_handler().
 *   - Context switches happen in PendSV (lowest priority). Threads run on
 *     PSP; handlers/kernel run on MSP.
 *   - sai_kernel_start() enters the first thread via SVC #0.
 */
#include "cortex_m.h"
#include "sai/port.h"
#include "sai/kernel.h"
#include "sai/time.h"
#include "tcb_offset.h"

#if !CONFIG_SAI_ARM_USE_BASEPRI
#error "arch/arm/m4 requires CONFIG_SAI_ARM_USE_BASEPRI (PRIMASK port: use arch/arm/m0plus)"
#endif

/* Kernel state shared with the assembly switch paths. */
extern sai_thread_t *_sai_current;
extern volatile bool _sai_preempt_hint;
extern sai_thread_t *_sai_schedule_pick(void);
extern void sai_thread_exit(void);

/* Pin the assembly offset against the real C layout so a TCB change can
 * never silently corrupt every context switch. */
_Static_assert(SAI_TCB_SP_OFFSET == __builtin_offsetof(sai_thread_t, sp),
               "SAI_TCB_SP_OFFSET does not match sai_thread_t.sp");
_Static_assert(SAI_TCB_SP_OFFSET + sizeof(void *)
                   == __builtin_offsetof(sai_thread_t, arch),
               "sai_thread_t.arch does not follow .sp");

/* ------------------------------------------------------------------ */
/* Register shims                                                      */
/* ------------------------------------------------------------------ */
#define SCS_BASE        0xE000E000u
#define SYST_CSR       (*(volatile uint32_t *)(SCS_BASE + 0x010u))
#define SYST_RVR       (*(volatile uint32_t *)(SCS_BASE + 0x014u))
#define SYST_CVR       (*(volatile uint32_t *)(SCS_BASE + 0x018u))
#define SCB_ICSR       (*(volatile uint32_t *)(SCS_BASE + 0xD04u))
#define SCB_SCR        (*(volatile uint32_t *)(SCS_BASE + 0xD10u))
#define SCB_SHPR3      (*(volatile uint32_t *)(SCS_BASE + 0xD20u))
#define NVIC_ISER0     (*(volatile uint32_t *)(SCS_BASE + 0x100u))
#define NVIC_ICER0     (*(volatile uint32_t *)(SCS_BASE + 0x180u))
#define NVIC_ICPR0     (*(volatile uint32_t *)(SCS_BASE + 0x280u))

#define SYST_CSR_CLKSOURCE (1u << 2)
#define SYST_CSR_TICKINT   (1u << 1)
#define SYST_CSR_ENABLE    (1u << 0)
#define SYST_CSR_COUNTFLAG (1u << 16)

#define ICSR_PENDSTSET  (1u << 26)
#define ICSR_PENDSTCLR  (1u << 25)
#define ICSR_PENDSVSET  (1u << 28)
#define ICSR_VECTACTIVE 0x1FFu

#define SCB_SCR_SLEEPDEEP (1u << 2)
#define SCB_SCR_SEVONPEND (1u << 4)

/* PendSV must run BELOW the BASEPRI threshold: a thread spinning in
 * port_switch() holds a kernel critical section and still has to be
 * switchable.  SysTick sits at the threshold, so ticks are deferred
 * until the next port_unlock() instead of preempting kernel state. */
#define PENDSV_PRIO     0xFFu
#define SYSTICK_PRIO    0xF0u

uint32_t s_critical_nesting;   /* non-static: zeroed by the switch paths */

/* ------------------------------------------------------------------ */
/* Critical sections                                                   */
/* ------------------------------------------------------------------ */
#if CONFIG_SAI_ARM_USE_BASEPRI
static inline void basepri_set(uint32_t v)
{
    __asm__ volatile ("msr basepri, %0" :: "r"(v));
}
#endif

uint32_t port_lock(void)
{
    uint32_t key;
#if CONFIG_SAI_ARM_USE_BASEPRI
    __asm__ volatile ("mrs %0, primask" : "=r"(key));
    basepri_set(CONFIG_SAI_BASEPRI_THRESHOLD);
#else
    uint32_t primask;
    __asm__ volatile ("mrs %0, primask" : "=r"(primask));
    key = primask;
    __asm__ volatile ("cpsid i" ::: "memory");
#endif
    s_critical_nesting++;
    __asm__ volatile ("" : : : "memory");
    return key;
}

void port_unlock(uint32_t key)
{
    __asm__ volatile ("" : : : "memory");
    if (--s_critical_nesting == 0u) {
#if CONFIG_SAI_ARM_USE_BASEPRI
        basepri_set(0u);
#else
        if (key & 1u) {
            __asm__ volatile ("cpsie i" ::: "memory");
        }
#endif
    }
}

bool port_in_isr(void)
{
    uint32_t ipsr;
    __asm__ volatile ("mrs %0, ipsr" : "=r"(ipsr));
    return (ipsr & ICSR_VECTACTIVE) != 0u;
}

/* ------------------------------------------------------------------ */
/* Spinlock (single core: BASEPRI-backed)                              */
/* ------------------------------------------------------------------ */
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

/* ------------------------------------------------------------------ */
/* Initial thread stack                                                */
/* ------------------------------------------------------------------ */
extern void sai_thread_exit_trampoline(void);

void *port_stack_init(void *stack_top, size_t stack_size,
                      sai_thread_entry_t entry, void *arg)
{
    (void)stack_size;
    uint32_t *sp = (uint32_t *)stack_top;

    /* ARMv7-M basic frame: xPSR, PC, LR, R12, R3, R2, R1, R0 (+FP regs for
     * the extended frame live below these when the FPU is in use; lazy
     * stacking handles that transparently for us since every thread first
     * enters through an exception return). */
    *--sp = 0x01000000u;                       /* xPSR: EPSR.T = 1 */
    *--sp = (uint32_t)entry;                   /* PC               */
    *--sp = (uint32_t)sai_thread_exit_trampoline; /* LR            */
    *--sp = 0u;                                /* R12              */
    *--sp = 0u;                                /* R3               */
    *--sp = 0u;                                /* R2               */
    *--sp = 0u;                                /* R1               */
    *--sp = (uint32_t)arg;                     /* R0               */
    for (int i = 0; i < 8; i++) {
        *--sp = 0u;                            /* R4-R11           */
    }
    return sp;
}

/* ------------------------------------------------------------------ */
/* Tick configuration (SysTick + tickless one-shot)                    */
/* ------------------------------------------------------------------ */
static uint32_t s_reload_ticks;    /* SysTick counts per tick */
static bool     s_oneshot_mode;

void port_timer_setup(uint32_t period_ms)
{
    /* SysTick runs from the core clock: CONFIG_SAI_CPU_CLOCK_HZ. */
    uint64_t load64 = ((uint64_t)CONFIG_SAI_CPU_CLOCK_HZ / 1000u) * period_ms;
    if (load64 > 0xFFFFFFull) {
        load64 = 0xFFFFFFull;              /* 24-bit: caller must pick a sane tick */
    }
    s_reload_ticks = (uint32_t)load64;
    s_oneshot_mode = false;

    SYST_RVR = s_reload_ticks - 1u;
    SYST_CVR = 0u;
    SYST_CSR = SYST_CSR_CLKSOURCE | SYST_CSR_TICKINT | SYST_CSR_ENABLE;
}

void port_timer_oneshot(uint32_t ticks)
{
    uint64_t load64 = ((uint64_t)CONFIG_SAI_CPU_CLOCK_HZ / 1000u)
                      * (uint64_t)ticks * (uint64_t)SAI_TICK_MS / (uint64_t)SAI_TICK_MS;
    if (ticks == 0u) {
        load64 = (uint64_t)CONFIG_SAI_CPU_CLOCK_HZ / 1000u;
    } else {
        load64 = ((uint64_t)CONFIG_SAI_CPU_CLOCK_HZ / 1000u) * (uint64_t)ticks;
    }
    if (load64 > 0xFFFFFFull) {
        load64 = 0xFFFFFFull;
    }
    s_reload_ticks = (uint32_t)load64;
    s_oneshot_mode = true;

    SYST_CSR = 0u;                          /* stop */
    SYST_RVR = s_reload_ticks - 1u;
    SYST_CVR = 0u;
    SYST_CSR = SYST_CSR_CLKSOURCE | SYST_CSR_TICKINT | SYST_CSR_ENABLE;
}

uint32_t port_cycle_count(void)
{
    return sai_tick_count();
}

/* ------------------------------------------------------------------ */
/* Idle / power                                                        */
/* ------------------------------------------------------------------ */
void port_idle_until_tick(void)
{
#if CONFIG_SAI_TICKLESS
    uint32_t next = _sai_next_deadline_ticks();
    if (next != 0u) {
        port_timer_oneshot(next);
    }
#endif
    SCB_SCR &= ~SCB_SCR_SLEEPDEEP;
    __asm__ volatile ("dsb\n wfi" ::: "memory");
}

void port_idle(void)
{
    port_idle_until_tick();
}

void port_idle_wait_for_interrupt(void)
{
    __asm__ volatile ("dsb\n wfi" ::: "memory");
}

/* ------------------------------------------------------------------ */
/* Scheduler hooks                                                     */
/* ------------------------------------------------------------------ */
void port_init(void)
{
    s_critical_nesting = 0u;
    /* SHPR3: bits[31:24] SysTick, bits[23:16] PendSV. */
    SCB_SHPR3 = ((uint32_t)SYSTICK_PRIO << 24) | ((uint32_t)PENDSV_PRIO << 16);
}

void port_thread_ready(sai_thread_t *t)
{
    /* ARM port: there is nothing to spawn.  The TCB gets a valid initial
     * frame from port_stack_init() (done by the kernel for main/idle and
     * by sai_thread_start() for dynamically created threads). */
    (void)t;
}

void port_main_returned(void)
{
    /* main_fn() returned: orderly shutdown.  main_thread_entry() calls us
     * while _sai_current == the (already dead) main thread, inside the
     * scheduler's port_lock() critical section.  Cortex-M has no process
     * to exit to: park the core with interrupts open. */
    for (;;) {
        __asm__ volatile ("dsb\n wfi" ::: "memory");
    }
}

void port_schedule_from_isr(void)
{
    SCB_ICSR = ICSR_PENDSVSET;              /* pend PendSV */
}

void port_start_first_thread(sai_thread_t *t)
{
    /* The boot context (reset MSP) becomes the temporary runner.  It
     * pends PendSV once: the handler saves nothing for this throwaway
     * context, selects the real first thread and exception-returns into
     * it on PSP.  From then on every SAI thread lives on its own PSP
     * stack and the boot MSP is never returned to. */
    (void)t;
    /* Break the scheduler's assumption that _sai_current describes a live
     * context: the boot context is thrown away, so let PendSV dispatch
     * with nothing to save (pick() handles cur == NULL as first boot). */
    _sai_current = NULL;
    __asm__ volatile ("dsb" ::: "memory");
    SCB_ICSR = ICSR_PENDSVSET;              /* first switch */
    for (;;) {
        __asm__ volatile ("wfi");           /* never reached */
    }
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */
__attribute__((weak)) void port_putchar(char c)
{
    (void)c;   /* board-level console provides real output (see boards/) */
}

void port_backtrace(void)
{
    uint32_t pc, lr, sp;
    __asm__ volatile ("mov %0, pc" : "=r"(pc));
    __asm__ volatile ("mov %0, lr" : "=r"(lr));
    __asm__ volatile ("mov %0, sp" : "=r"(sp));
    sai_printf_safe("  pc=0x%08x lr=0x%08x sp=0x%08x\n", pc, lr, sp);
}

void port_halt(void)
{
    for (;;) {
        __asm__ volatile ("wfi");
    }
}

/* ------------------------------------------------------------------ */
/* SysTick handler (called from the vector table)                      */
/* ------------------------------------------------------------------ */
void SysTick_Handler(void)
{
    if (_sai_current == NULL) {
        return;                             /* timer live before boot */
    }
    _sai_tick_handler();
    if (s_oneshot_mode) {
        /* tickless: compensate elapsed and reprogram via the kernel */
        _sai_tickless_rearm();
    }
}

/* ------------------------------------------------------------------ */
/* PendSV: the ONE switch primitive (see context_switch.S)             */
/* ------------------------------------------------------------------ */
__attribute__((naked)) void PendSV_Handler(void)
{
    __asm__ volatile (
        "push {lr}                     \n"   /* EXC_RETURN survives the call */
        "ldr r0, =_sai_current         \n"
        "ldr r1, [r0]                  \n"   /* r1 = from                     */
        "cmp r1, #0                    \n"
        "beq 1f                        \n"
        "mrs r2, psp                   \n"
        "stmdb r2!, {r4-r11}           \n"
        "str r2, [r1, #60]             \n"   /* from->sp (SAI_TCB_SP_OFFSET)  */
        "mov r3, #0                    \n"
        "str r3, [r1, #64]             \n"   /* from->arch = NULL: rendezvous */
        "ldr r2, =s_critical_nesting   \n"
        "str r3, [r2]                  \n"   /* the switch IS the unlock      */
        "1:                            \n"
        "bl _sai_schedule_pick         \n"   /* updates _sai_current          */
        "ldr r0, =_sai_current         \n"
        "ldr r1, [r0]                  \n"   /* r1 = to                       */
        "ldr r2, [r1, #60]             \n"   /* to->sp                        */
        "msr psp, r2                   \n"
        "ldmia.w r2!, {r4-r11}         \n"
        "mov r0, #0                    \n"
        "msr basepri, r0               \n"   /* open the critical section     */
        "pop {lr}                      \n"
        "bx lr                         \n"
        ::: "memory"
    );
}
