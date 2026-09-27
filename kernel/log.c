/**
 * @file kernel/log.c
 * @brief Logging, assertions and panic/fault diagnostics.
 */
#include "internal.h"
#include <sai/console.h>

static sai_log_level_t s_level = SAI_LOG_INFO;
static sai_panic_hook_t s_panic_hook = NULL;
int sai_errno = 0;

void sai_log_set_level(sai_log_level_t level) { s_level = level; }
sai_log_level_t sai_log_get_level(void) { return s_level; }

static const char *level_tag(sai_log_level_t l)
{
    switch (l) {
    case SAI_LOG_ERROR: return "E";
    case SAI_LOG_WARN:  return "W";
    case SAI_LOG_INFO:  return "I";
    case SAI_LOG_DEBUG: return "D";
    default:            return "?";
    }
}

#if defined(SAI_HOST_BUILD)
/* host: plain printf backend, no locking needed (stdout is MT-safe enough) */
void sai_log_printf(sai_log_level_t level, const char *tag,
                    const char *fmt, ...)
{
    if (level > s_level) {
        return;
    }
    char line[CONFIG_SAI_LOG_LINE_MAX];
    va_list ap;
    va_start(ap, fmt);
    (void)sai_vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    sai_printf("[%s] %s: %s\n", level_tag(level), tag ? tag : "sai", line);
}
#else
/* target: emit through the console with a tiny tick stamp */
void sai_log_printf(sai_log_level_t level, const char *tag,
                    const char *fmt, ...)
{
    if (level > s_level) {
        return;
    }
    char line[CONFIG_SAI_LOG_LINE_MAX];
    va_list ap;
    va_start(ap, fmt);
    (void)sai_vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    sai_printf("[%8u][%s] %s: %s\n",
               (unsigned)sai_tick_count(), level_tag(level), tag ? tag : "sai", line);
}
#endif

/* ------------------------------------------------------------------ */
/* Assertions / panic                                                  */
/* ------------------------------------------------------------------ */
void sai_assert_failed(const char *file, int line, const char *expr)
{
    sai_panic("assertion failed", file, line);
    (void)expr;
}

SAI_NORETURN void sai_panic_format(const char *file, int line,
                                   const char *fmt, ...)
{
    char buf[CONFIG_SAI_LOG_LINE_MAX];
    va_list ap;
    va_start(ap, fmt);
    (void)sai_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    sai_panic(buf, file, line);
}

void _sai_panic_print(const char *reason, const char *file, int line)
{
    sai_console_write("\n*** KERNEL PANIC ***\n");
    sai_printf("reason: %s\n", reason ? reason : "unknown");
    if (file != NULL) {
        sai_printf("  at: %s:%d\n", file, line);
    }
    if (_sai_current != NULL) {
        sai_printf("thread: %s (prio %u, state %u)\n",
                   _sai_current->kobj.name ? _sai_current->kobj.name : "?",
                   (unsigned)_sai_current->prio,
                   (unsigned)_sai_current->state);
    } else {
        sai_printf("thread: (none - pre-scheduler)\n");
    }
    sai_printf(" tick: %u\n", (unsigned)_sai_tick_count);
#if CONFIG_SAI_HEAP_SIZE > 0
    sai_printf(" heap free: %u, max block: %u, frag %u%%\n",
               (unsigned)sai_mem_free_bytes(),
               (unsigned)sai_mem_max_free_block(),
               (unsigned)sai_mem_fragmentation_pct());
#endif
}

void sai_panic(const char *reason, const char *file, int line)
{
    port_lock();                            /* interrupts off from here on */
    _sai_panic_print(reason, file, line);
    port_backtrace();
    if (s_panic_hook != NULL) {
        s_panic_hook(reason);
    }
    sai_console_write("*** system halted ***\n");
    port_halt();
    for (;;) {
    }
}

void sai_panic_set_hook(sai_panic_hook_t hook)
{
    s_panic_hook = hook;
}

#if defined(SAI_HOST_BUILD)
void sai_halt_cpu(void)
{
    port_halt();
}
#endif
