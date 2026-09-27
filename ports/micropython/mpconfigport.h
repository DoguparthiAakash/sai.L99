/*
 * ports/micropython/mpconfigport.h
 * MicroPython configuration for the sai.L99 RTOS port.
 *
 * This single header is shared by the host build and the ARM target build;
 * everything arch-dependent is gated on SAI_HOST_BUILD.  The vendored core
 * is MicroPython v1.25.0 (MIT, see third_party/micropython/LICENSE).
 */
#ifndef SAI_MPCONFIGPORT_H
#define SAI_MPCONFIGPORT_H

#ifdef _MSC_VER
/* MSVC has no __BYTE_ORDER__ and no <endian.h>; both host x86-64 and the
 * ARM target are little-endian.  Defining this keeps mpconfig.h from
 * including <endian.h>. */
#define MP_ENDIANNESS_LITTLE (1)
#endif

/* Feature level: "core features" gives the full compiler + core builtins;
 * we then disable filesystem/stream-heavy machinery (an RTOS without a VFS). */
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_CORE_FEATURES)

/* --- compiler / runtime ------------------------------------------------ */
#define MICROPY_ENABLE_COMPILER              (1)
#define MICROPY_ENABLE_GC                    (1)
#define MICROPY_ENABLE_EXTERNAL_IMPORT       (0)
#define MICROPY_ENABLE_DOC_STRING            (0)
#define MICROPY_ENABLE_SOURCE_LINE           (0)
#define MICROPY_ENABLE_EMERGENCY_EXCEPTION_BUF (1)
#define MICROPY_MULTIPLE_THREADS             (0)
#define MICROPY_VFS                          (0)
#define MICROPY_READER_VFS                   (0)
#define MICROPY_PLAT_ALLOC_EXEC              (0)
#define MICROPY_PLAT_FREE_EXEC               (0)
#define MICROPY_WARNINGS                     (0)
#define MICROPY_PY_BUILTINS_HELP             (0)
#define MICROPY_COMP_TRIPLE_TUPLE_ASSIGN     (0)

/* --- optional modules -------------------------------------------------- */
#define MICROPY_PY_IO                        (0)
#define MICROPY_PY_UTIME                     (0)   /* we provide sai.* */
#define MICROPY_PY_UERRNO                    (0)
#define MICROPY_PY_UCTYPES                   (0)
#define MICROPY_PY_UZLIB                     (0)
#define MICROPY_PY_UJSON                     (0)
#define MICROPY_PY_URE                       (0)
#define MICROPY_PY_UHEAPQ                    (0)
#define MICROPY_PY_UBINASCII                 (0)
#define MICROPY_PY_URANDOM                   (0)
#define MICROPY_PY_UASYNCIO                  (0)
#define MICROPY_PY_UCRYPTLIB                 (0)
#define MICROPY_PY_USELECT                   (0)
#define MICROPY_PY_FRAMES                    (0)
#define MICROPY_PY_GC                        (1)
#define MICROPY_PY_MATH                      (1)
#define MICROPY_PY_CMATH                     (0)
#define MICROPY_PY_STRUCT                    (1)
#define MICROPY_PY_SYS                       (1)
#define MICROPY_PY_ARRAY                     (1)
#define MICROPY_PY_COLLECTIONS               (1)
#define MICROPY_PY_MICROPYTHON               (1)

/* --- exceptions / errors ----------------------------------------------- */
#define MICROPY_ERROR_REPORTING              (MICROPY_ERROR_REPORTING_TERSE)
#define MICROPY_USE_INTERNAL_ERRNO           (1)

/* --- non-blocking streams: none (no streams at all) -------------------- */
#define MICROPY_STREAMS_NON_BLOCK            (0)

/* --- numeric support ----------------------------------------------------- */
#ifdef SAI_HOST_BUILD
#define MICROPY_FLOAT_IMPL                   (MICROPY_FLOAT_IMPL_FLOAT)
#define MICROPY_LONGINT_IMPL                 (MICROPY_LONGINT_IMPL_LONGLONG)
#else
/* Cortex-M4 (soft ABI): keep the image small -- machine ints only */
#define MICROPY_FLOAT_IMPL                   (MICROPY_FLOAT_IMPL_NONE)
#define MICROPY_LONGINT_IMPL                 (MICROPY_LONGINT_IMPL_NONE)
#endif

/* --- nlr: native on target, setjmp on the host -------------------------- */
#ifdef SAI_HOST_BUILD
#define MICROPY_NLR_SETJMP                   (1)
#else
#define MICROPY_NLR_SETJMP                   (0)   /* nlrthumb.c */
#endif

/* --- type definitions --------------------------------------------------- */
typedef int32_t mp_int_t;      /* must be pointer size */
typedef uint32_t mp_uint_t;    /* must be pointer size */
#ifdef SAI_HOST_BUILD
typedef long long mp_off_t;
#else
typedef int32_t mp_off_t;
#endif
#define MP_SSIZE_MAX (0x7fffffff)

/* We need at least a printf-compatible vprintf for mp_plat_print on the
 * host; the port routes everything through sai's own printer anyway. */
#define MICROPY_DEBUG_PRINTERS               (0)

/* --- stack growth (alloca) ---------------------------------------------- */
#ifdef SAI_HOST_BUILD
#ifdef _WIN32
#include <malloc.h>    /* alloca on MSVC / mingw */
#else
#include <alloca.h>
#endif
#else
#define alloca __builtin_alloca
#endif

/* --- misc --------------------------------------------------------------- */
#define MICROPY_ALLOC_PATH_MAX               (128)
#define MICROPY_QSTR_BYTES_IN_LEN            (1)
#define MICROPY_ALLOC_GC_STACK_SIZE          (64)
#define MICROPY_GC_STACK_ENTRY_TYPE          uint16_t

#endif /* SAI_MPCONFIGPORT_H */
