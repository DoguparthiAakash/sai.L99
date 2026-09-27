/**
 * @file libc/stdbool.h
 * @brief stdbool shim: guarantees bool/true/false in C builds.
 *
 * MSVC in C11 mode lacks <stdbool.h> (bool is only a keyword in C23); this
 * shim provides the type where needed and stays a no-op elsewhere.
 */
#ifndef __SAI_LIBC_STDBOOL_H
#define __SAI_LIBC_STDBOOL_H

#if !defined(__cplusplus) && !defined(__bool_true_false_are_defined)
#include <stdint.h>
typedef uint8_t bool;
#define true  1
#define false 0
#define __bool_true_false_are_defined 1
#endif

#endif
