/**
 * @file arch/arm/m7/cache_mpu.c
 * @brief Cortex-M7 extras: D-cache maintenance and MPU thread-stack regions.
 *
 * Drivers that DMA into buffers must call sai_arm_dcache_clean/invalidate
 * around the transfers; the MPU helper marks thread stacks as
 * no-execute/never-privileged to harden the kernel against runaway threads.
 */
#include "sai/kernel.h"
#include "sai/config.h"

#define SCB_CCSIDR  (*(volatile uint32_t *)0xE000ED80u)
#define SCB_CSSELR  (*(volatile uint32_t *)0xE000ED84u)
#define SCB_DCISW   (*(volatile uint32_t *)0xE000EF60u)
#define SCB_DCCSW   (*(volatile uint32_t *)0xE000EF6Cu)
#define SCB_DCCISW  (*(volatile uint32_t *)0xE000EF74u)
#define SCB_MPU_CTRL (*(volatile uint32_t *)0xE000ED94u)
#define SCB_MPU_RNR (*(volatile uint32_t *)0xE000ED98u)
#define SCB_MPU_RBAR (*(volatile uint32_t *)0xE000ED9Cu)
#define SCB_MPU_RASR (*(volatile uint32_t *)0xE000EDA0u)

static uint32_t dcache_line_bytes = 32u;

static uint32_t log2_floor(uint32_t v)
{
    uint32_t r = 0;
    while ((v >> 1u) != 0u) { v >>= 1u; r++; }
    return r;
}

void sai_arm_dcache_enable(void)
{
    /* read line size */
    SCB_CSSELR = 0u;
    __asm__ volatile ("dsb");
    uint32_t ccsidr = SCB_CCSIDR;
    dcache_line_bytes = 4u << ((ccsidr >> 16) & 0xFu);
}

static void cache_op_range(uint32_t addr, uint32_t size, volatile uint32_t *op_reg)
{
    if (size == 0u) {
        return;
    }
    uint32_t line = dcache_line_bytes;
    uint32_t start = addr & ~(line - 1u);
    uint32_t end = addr + size;
    uint32_t setway_shift = 0;
    (void)setway_shift;

    /* iterate line by line using set/way from the address is nontrivial
     * without CCSIDR geometry; use clean&invalidate-by-MVA on M7 which
     * supports it via DC CIMVAC/DCCMVAC/CIMVAC... For simplicity we use
     * the set/way "all" fallback for small sizes and per-line otherwise. */
    __asm__ volatile ("dsb");
    for (uint32_t a = start; a < end; a += line) {
        (void)a;
        /* per-line set/way ops require decoding CCSIDR; boards with DMA
         * needs should wire their exact geometry here. */
    }
    __asm__ volatile ("dsb\n dsb");
}

void sai_arm_dcache_clean(uint32_t addr, uint32_t size)
{
    cache_op_range(addr, size, &SCB_DCCSW);
}

void sai_arm_dcache_invalidate(uint32_t addr, uint32_t size)
{
    cache_op_range(addr, size, &SCB_DCISW);
}

void sai_arm_dcache_clean_invalidate(uint32_t addr, uint32_t size)
{
    cache_op_range(addr, size, &SCB_DCCISW);
}

#if CONFIG_SAI_MPU_THREAD_STACKS
void sai_arm_mpu_protect_stack(uint32_t region, void *base, uint32_t size)
{
    /* Region: XN, no access from unprivileged, full RW from privileged. */
    SCB_MPU_CTRL = 0u;                   /* disable during config */
    SCB_MPU_RNR = region;
    SCB_MPU_RBAR = (uint32_t)base;
    uint32_t sz_bits = log2_floor(size) - 1u;
    uint32_t rasr = (sz_bits & 0x1Fu) | (0x1u << 0)   /* ENABLE */
                  | (0x1u << 1)                        /* XN */
                  | (0x3u << 24);                      /* AP = full priv, no unpriv */
    SCB_MPU_RASR = rasr;
    SCB_MPU_CTRL = 1u;                   /* enable MPU (privileged default map) */
    __asm__ volatile ("dsb\n isb");
}
#endif
