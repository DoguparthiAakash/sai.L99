/**
 * @file libc/limits.h
 * @brief Freestanding limits.h shim.
 */
#ifndef __SAI_LIBC_LIMITS_H
#define __SAI_LIBC_LIMITS_H

/* Let the toolchain provide the C standard macros first. */
#if defined(__GNUC__) || defined(__clang__)
#include_next <limits.h>
#else
/* MSVC has no builtin limits.h include_next; provide the common macros. */
#  define CHAR_BIT  8
#  define SCHAR_MIN (-128)
#  define SCHAR_MAX 127
#  define UCHAR_MAX 255
#  define SHRT_MIN  (-32768)
#  define SHRT_MAX  32767
#  define USHRT_MAX 65535
#  define INT_MIN   (-2147483647 - 1)
#  define INT_MAX   2147483647
#  define UINT_MAX  4294967295u
#  define LONG_MIN  (-2147483647L - 1)
#  define LONG_MAX  2147483647L
#  define ULONG_MAX 4294967295ul
#  define LLONG_MIN (-9223372036854775807LL - 1)
#  define LLONG_MAX 9223372036854775807LL
#  define ULLONG_MAX 18446744073709551615ull
#endif

#endif
