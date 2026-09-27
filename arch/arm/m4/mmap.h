/**
 * @file arch/arm/m4/mmap.h
 * @brief ARMv7-M memory map constants (shared with m7).
 */
#ifndef SAI_ARM_MMAP_H
#define SAI_ARM_MMAP_H

/* Cortex-M standard memory map */
#define SAI_ARM_FLASH_BASE   0x08000000u
#define SAI_ARM_SRAM_BASE    0x20000000u
#define SAI_ARM_PERIPH_BASE  0x40000000u
#define SAI_ARM_PRIV_BASE    0xE0000000u

#define SAI_ARM_NVIC_BASE    0xE000E000u
#define SAI_ARM_SCS_BASE     0xE000E000u
#define SAI_ARM_SYSTICK_BASE 0xE000E010u
#define SAI_ARM_SCB_BASE     0xE000ED00u

/* Vector table: first two words are initial SP and reset vector. */
#define SAI_ARM_VTOR_OFFSET_SP   0x0u
#define SAI_ARM_VTOR_OFFSET_RESET 0x4u

#endif /* SAI_ARM_MMAP_H */
