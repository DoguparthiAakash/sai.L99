/**
 * @file libc/stdatomic.h
 * @brief Freestanding stdatomic.h shim.
 *
 * On GCC/Clang >= 4.9 the native header exists; the shim just forwards.
 * On MSVC (no _Atomic in C mode) a volatile-based fallback is provided.
 */
#ifndef __SAI_LIBC_STDATOMIC_H
#define __SAI_LIBC_STDATOMIC_H

#if defined(__GNUC__) || defined(__clang__)
#include_next <stdatomic.h>
#else
/* MSVC C-mode fallback: the kernel core never uses C11 _Atomic, but some
 * driver code may include this header for atomic_flag-style operations. */
#include <stdint.h>

typedef volatile long _Atomic(long);
typedef volatile uint32_t _Atomic(uint32_t);
typedef volatile uint16_t _Atomic(uint16_t);
typedef volatile uint8_t _Atomic(uint8_t);
typedef volatile uint64_t _Atomic(uint64_t);
typedef volatile int _Atomic(int);
typedef volatile unsigned _Atomic(unsigned);

#define ATOMIC_VAR_INIT(v) (v)
#define atomic_init(p, v)  (*(p) = (v))

static inline uint32_t atomic_load_u32(volatile uint32_t *p) { return *p; }
static inline void atomic_store_u32(volatile uint32_t *p, uint32_t v) { *p = v; }
static inline uint32_t atomic_fetch_add_u32(volatile uint32_t *p, uint32_t v)
{
    return (uint32_t)_InterlockedExchangeAdd((volatile long *)p, (long)v);
}
static inline uint32_t atomic_fetch_sub_u32(volatile uint32_t *p, uint32_t v)
{
    return (uint32_t)_InterlockedExchangeAdd((volatile long *)p, -(long)v);
}
#endif

#endif /* __SAI_LIBC_STDATOMIC_H */
