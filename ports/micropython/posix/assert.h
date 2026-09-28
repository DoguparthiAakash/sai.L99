/*
 * ports/micropython/posix/assert.h
 * Freestanding shim for the MicroPython core: routes the assert macro to
 * the kernel's port_halt() so failed assertions stop the RTOS visibly.
 */
#ifndef SAI_MP_POSIX_ASSERT_H
#define SAI_MP_POSIX_ASSERT_H

#include <sai/kernel.h>

#ifdef NDEBUG
#define assert(x) ((void)0)
#else
extern void mp_assert_fail(const char *expr, const char *file, int line);
#define assert(x) \
    ((x) ? (void)0 : mp_assert_fail(#x, __FILE__, __LINE__))
#endif

#endif /* SAI_MP_POSIX_ASSERT_H */
