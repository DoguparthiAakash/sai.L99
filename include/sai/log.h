/**
 * @file sai/log.h
 * @brief Logging, assertion and panic/fault-diagnostic interface.
 */
#ifndef SAI_LOG_H
#define SAI_LOG_H

#include <sai/types.h>
#include <sai/config.h>
#include <sai/version.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SAI_LOG_LEVEL_NONE = 0,
    SAI_LOG_ERROR,
    SAI_LOG_WARN,
    SAI_LOG_INFO,
    SAI_LOG_DEBUG,
} sai_log_level_t;

/** Set the minimum level that is emitted. */
void sai_log_set_level(sai_log_level_t level);
/** Current minimum level. */
sai_log_level_t sai_log_get_level(void);

/**
 * Core log function (level, subsystem tag, printf-style format).
 * Safe from any context except ISRs above the log lock; the default backend
 * serializes with a spinlock and drops characters if not yet initialized.
 */
void sai_log_printf(sai_log_level_t level, const char *tag,
                    const char *fmt, ...) SAI_FORMAT_PRINTF(3, 4);

#if CONFIG_SAI_LOG
#define SAI_LOG(level, tag, ...) \
    do { if ((level) <= sai_log_get_level()) sai_log_printf((level), (tag), __VA_ARGS__); } while (0)
#else
#define SAI_LOG(level, tag, ...) do { } while (0)
#endif

#define SAI_LOGE(tag, ...) SAI_LOG(SAI_LOG_ERROR, tag, __VA_ARGS__)
#define SAI_LOGW(tag, ...) SAI_LOG(SAI_LOG_WARN,   tag, __VA_ARGS__)
#define SAI_LOGI(tag, ...) SAI_LOG(SAI_LOG_INFO, tag, __VA_ARGS__)
#define SAI_LOGD(tag, ...) SAI_LOG(SAI_LOG_DEBUG, tag, __VA_ARGS__)

/* ------------------------------------------------------------------ */
/* Assertions                                                          */
/* ------------------------------------------------------------------ */
/** Kernel assertion; on failure calls sai_panic("assert", ...). */
SAI_NORETURN void sai_assert_failed(const char *file, int line, const char *expr);

#if CONFIG_SAI_ASSERT
#define SAI_ASSERT(x) \
    do { if (!(x)) sai_assert_failed(__FILE__, __LINE__, #x); } while (0)
#else
#define SAI_ASSERT(x) do { } while (0)
#endif

/** Debug-only assertion (independent of CONFIG_SAI_ASSERT). */
#if CONFIG_SAI_DEBUG
#define SAI_DASSERT(x) \
    do { if (!(x)) sai_assert_failed(__FILE__, __LINE__, #x); } while (0)
#else
#define SAI_DASSERT(x) do { } while (0)
#endif

/* ------------------------------------------------------------------ */
/* Panic / fault diagnostics                                           */
/* ------------------------------------------------------------------ */
/**
 * Kernel panic: prints diagnostics (file/line, expr, thread, backtrace when
 * supported) and halts. Never returns.
 */
SAI_NORETURN void sai_panic(const char *reason, const char *file, int line);


/** Convenience wrapper: SAI_PANIC("reason fmt %d", v); */
#define SAI_PANIC(...) \
    do { sai_panic_format(__FILE__, __LINE__, __VA_ARGS__); } while (0)

SAI_NORETURN void sai_panic_format(const char *file, int line,
                                   const char *fmt, ...)
    SAI_FORMAT_PRINTF(3, 4);

/** Registers a fatal-error hook run before the panic halt loop. */
typedef void (*sai_panic_hook_t)(const char *reason);
void sai_panic_set_hook(sai_panic_hook_t hook);

/** Halt the CPU (wfi on Cortex-M; port-provided). */
SAI_NORETURN void sai_halt_cpu(void);

#ifdef __cplusplus
}
#endif

#endif /* SAI_LOG_H */
