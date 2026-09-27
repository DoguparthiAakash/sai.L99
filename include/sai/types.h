/**
 * @file sai/types.h
 * @brief Fundamental types, limits and status codes for the sai.L99 RTOS.
 *
 * sai.L99 is an independent RTOS written from scratch in C11. This header
 * defines the basic fixed-width types used across the public API, the
 * scheduling constants and common status codes.
 */
#ifndef SAI_TYPES_H
#define SAI_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Compiler portability macros (avoid compiler-specific extensions in core)
 * ----------------------------------------------------------------------- */
#if defined(__GNUC__) || defined(__clang__)
#  define SAI_ALWAYS_INLINE   static inline __attribute__((always_inline))
#  define SAI_NOINLINE        __attribute__((noinline))
#  define SAI_NORETURN        __attribute__((noreturn))
#  define SAI_UNREACHABLE()   __builtin_unreachable()
#  define SAI_UNLIKELY(x)     __builtin_expect(!!(x), 0)
#  define SAI_LIKELY(x)       __builtin_expect(!!(x), 1)
#elif defined(_MSC_VER)
#  define SAI_ALWAYS_INLINE   static __forceinline
#  define SAI_NOINLINE        __declspec(noinline)
#  define SAI_NORETURN        __declspec(noreturn)
#  define SAI_UNREACHABLE()   __assume(0)
#  define SAI_UNLIKELY(x)     (x)
#  define SAI_LIKELY(x)       (x)
#else
#  define SAI_ALWAYS_INLINE   static inline
#  define SAI_NOINLINE
#  define SAI_NORETURN
#  define SAI_UNREACHABLE()   do { } while (0)
#  define SAI_UNLIKELY(x)     (x)
#  define SAI_LIKELY(x)       (x)
#endif

/** Marks a function whose result must be checked (GCC/clang warn otherwise). */
#if defined(__GNUC__) || defined(__clang__)
#  define SAI_WARN_UNUSED __attribute__((warn_unused_result))
#  define SAI_FORMAT_PRINTF(f, v) __attribute__((format(printf, f, v)))
#else
#  define SAI_WARN_UNUSED
#  define SAI_FORMAT_PRINTF(f, v)
#endif

/** Placed on definitions to request placement in fast internal RAM (best effort). */
#ifndef SAI_FASTCODE
#  define SAI_FASTCODE
#endif

/* --------------------------------------------------------------------------
 * Scheduling limits
 * ----------------------------------------------------------------------- */
#define SAI_NUM_PRIORITIES      32u   /**< 0 = highest (critical), 31 = idle. */
#define SAI_IDLE_PRIORITY       31u   /**< Reserved idle-thread priority.     */

/** Sentinel for "wait forever" in all blocking APIs. */
#define SAI_WAIT_FOREVER        (-1)
/** Sentinel for "do not wait at all". */
#define SAI_NO_WAIT             (0)

/* --------------------------------------------------------------------------
 * Status codes
 * ----------------------------------------------------------------------- */
typedef enum {
    SAI_OK              =  0,   /**< Success.                                   */
    SAI_ERR_INVAL       = -1,   /**< Invalid argument.                          */
    SAI_ERR_NOENT       = -2,   /**< No such entity.                            */
    SAI_ERR_NOMEM       = -3,   /**< Out of memory.                             */
    SAI_ERR_BUSY        = -4,   /**< Resource busy.                             */
    SAI_ERR_TIMEOUT     = -5,   /**< Timed out while waiting.                   */
    SAI_ERR_WOULD_BLOCK = -6,   /**< Non-blocking call would block.             */
    SAI_ERR_AGAIN       = -7,   /**< Temporary condition, retry.                */
    SAI_ERR_PERM        = -8,   /**< Operation not permitted (e.g. in ISR).     */
    SAI_ERR_STATE       = -9,   /**< Object in wrong state.                     */
    SAI_ERR_FULL        = -10,  /**< Container full.                            */
    SAI_ERR_EMPTY       = -11,  /**< Container empty.                           */
    SAI_ERR_NOINIT      = -12,  /**< Object/subsystem not initialised.          */
    SAI_ERR_BOUNDS      = -13,  /**< Value out of bounds.                       */
    SAI_ERR_IO          = -14,  /**< Device I/O error.                          */
    SAI_ERR_NOTSUP      = -15,  /**< Not supported by this configuration/port.  */
    SAI_ERR_DEADLOCK    = -16,  /**< Would self-deadlock.                       */
} sai_status_t;

/** Returns true when @p s is a success status. */
SAI_ALWAYS_INLINE bool sai_status_ok(sai_status_t s)
{
    return s == SAI_OK;
}

/* --------------------------------------------------------------------------
 * Kernel object header
 *
 * Every kernel object (thread, mutex, queue, ...) embeds this. It is opaque
 * to applications and used by the kernel for bookkeeping and diagnostics.
 * ----------------------------------------------------------------------- */
typedef struct sai_kobj {
    struct sai_kobj *next;          /**< Global registry linkage (diagnostics). */
    uint16_t         type;          /**< Object type (sai_kobj_type).           */
    uint16_t         flags;         /**< Internal flags.                        */
    const char      *name;          /**< Static name (not copied).              */
} sai_kobj_t;

/** Object types, for diagnostics and debugging. */
typedef enum {
    SAI_KOBJ_NONE = 0,
    SAI_KOBJ_THREAD,
    SAI_KOBJ_MUTEX,
    SAI_KOBJ_SEM,
    SAI_KOBJ_COND,
    SAI_KOBJ_SPINLOCK,
    SAI_KOBJ_MSGQ,
    SAI_KOBJ_MBOX,
    SAI_KOBJ_PIPE,
    SAI_KOBJ_EVENT,
    SAI_KOBJ_TIMER,
    SAI_KOBJ_MEMPOOL,
    SAI_KOBJ_SLAB,
    SAI_KOBJ_HEAP,
    SAI_KOBJ_DEVICE,
} sai_kobj_type_t;

#ifdef __cplusplus
}
#endif

#endif /* SAI_TYPES_H */
