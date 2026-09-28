/*
 * ports/micropython/posix/stdio.h
 * Freestanding stdio shim for the MicroPython core on sai.L99: the few
 * libc-stdio symbols the core may reference route to the sai console.
 * Implementations live in ports/micropython/sai_mp_port.c.
 */
#ifndef SAI_MP_POSIX_STDIO_H
#define SAI_MP_POSIX_STDIO_H

#include <stddef.h>
#include <stdarg.h>

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef struct mp_posix_file FILE;
#define stdin  ((FILE *)0)
#define stdout ((FILE *)0)
#define stderr ((FILE *)0)

int printf(const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int puts(const char *s);
int putchar(int c);

#endif /* SAI_MP_POSIX_STDIO_H */
