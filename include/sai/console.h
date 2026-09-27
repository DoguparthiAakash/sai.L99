/**
 * @file sai/console.h
 * @brief Console output (printf-style) over the log/serial backend.
 *
 * The console is the lowest-common-denominator output device: on the ARM
 * target it is polled UART; on the host it is printf(). This is what the
 * samples and the shell-lite print through.
 */
#ifndef SAI_CONSOLE_H
#define SAI_CONSOLE_H

#include <sai/types.h>
#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize the console backend (called during board bring-up). */
void sai_console_init(void);

/** snprintf/vsnprintf core (also used by the log subsystem). */
int32_t sai_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int32_t sai_snprintf(char *buf, size_t size, const char *fmt, ...);

/** printf to the console. */
void sai_printf(const char *fmt, ...) SAI_FORMAT_PRINTF(1, 2);

/** vprintf to the console. */
void sai_vprintf(const char *fmt, va_list ap);

/** Write a raw string (no formatting). */
void sai_console_write(const char *s);

#ifdef __cplusplus
}
#endif

#endif /* SAI_CONSOLE_H */
