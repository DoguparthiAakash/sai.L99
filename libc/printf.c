/**
 * @file libc/printf.c
 * @brief Compact printf-family implementation (no FP by default) plus the
 *        sai console backend.
 *
 * Supported conversions: %d %i %u %x %X %p %s %c %% with flags '-0+ #',
 * width (including '*'), and precision for strings/ints. Long modifiers
 * l/ll/z/size_t map to 32-bit on all supported targets.
 */
#include "internal.h"
#include <sai/console.h>
#include <stdint.h>

#define F_MINUS 0x1u
#define F_PLUS  0x2u
#define F_SPACE 0x4u
#define F_HASH  0x8u
#define F_ZERO  0x10u

/* ------------------------------------------------------------------ */
/* Formatting core                                                     */
/* ------------------------------------------------------------------ */
#define F_MINUS 0x1u
#define F_PLUS  0x2u
#define F_SPACE 0x4u
#define F_HASH  0x8u
#define F_ZERO  0x10u

typedef struct {
    char  *buf;
    size_t size;      /* total capacity */
    size_t pos;       /* bytes written (excl. NUL) */
    int    truncated;
} out_t;

static void out_ch(out_t *o, char c)
{
    if (o->pos + 1u < o->size) {
        o->buf[o->pos] = c;
    } else {
        o->truncated = true;
    }
    o->pos++;
}

static void out_str(out_t *o, const char *s, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        out_ch(o, s[i]);
    }
}

static void pad(out_t *o, char c, size_t n)
{
    while (n--) {
        out_ch(o, c);
    }
}

static int fmt_u32(char *tmp, uint32_t v, uint32_t base, int upper)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    size_t n = 0;
    do {
        tmp[n++] = digits[v % base];
        v /= base;
    } while (v != 0u);
    /* reverse in place */
    for (size_t i = 0; i < n / 2u; i++) {
        char t = tmp[i];
        tmp[i] = tmp[n - 1u - i];
        tmp[n - 1u - i] = t;
    }
    return n;
}

static void emit_num(out_t *o, uint32_t v, uint32_t base, int upper,
                     int negative, int width, int prec, uint32_t flags)
{
    char tmp[12];
    char prefix[2];
    size_t plen = 0;
    size_t len = fmt_u32(tmp, v, base, upper);

    if (negative) {
        prefix[plen++] = '-';
    } else if (flags & F_PLUS) {
        prefix[plen++] = '+';
    } else if (flags & F_SPACE) {
        prefix[plen++] = ' ';
    }
    (void)plen;
    if ((flags & F_HASH) && base == 16u && v != 0u) {
        prefix[plen++] = '0';
        prefix[plen++] = upper ? 'X' : 'x';
    }

    size_t zeros = 0;
    if (prec >= 0 && (size_t)prec > len) {
        zeros = (size_t)prec - len;
    }
    size_t total = plen + zeros + len;
    size_t padding = (width > 0 && (size_t)width > total) ? (size_t)width - total : 0u;

    if ((flags & F_MINUS) == 0u) {
        pad(o, (flags & F_ZERO) ? '0' : ' ', padding);
    }
    out_str(o, prefix, plen);
    pad(o, '0', zeros);
    out_str(o, tmp, len);
    if (flags & F_MINUS) {
        pad(o, ' ', padding);
    }
}

#define F_MINUS 0x1u
#define F_PLUS  0x2u
#define F_SPACE 0x4u
#define F_HASH  0x8u
#define F_ZERO  0x10u

static int32_t vsnprintf_impl(out_t *o, const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            out_ch(o, *fmt);
            continue;
        }
        fmt++;

        /* flags */
        uint32_t flags = 0;
        for (;; fmt++) {
            if (*fmt == '-')      { flags |= F_MINUS; }
            else if (*fmt == '+') { flags |= F_PLUS; }
            else if (*fmt == ' ') { flags |= F_SPACE; }
            else if (*fmt == '#') { flags |= F_HASH; }
            else if (*fmt == '0') { flags |= F_ZERO; }
            else { break; }
        }

        /* width */
        int width = 0;
        bool width_star = false;
        if (*fmt == '*') {
            width_star = true;
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }
        }

        /* precision */
        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') {
                fmt++;
                prec = va_arg(ap, int);
            } else {
                while (*fmt >= '0' && *fmt <= '9') {
                    prec = prec * 10 + (*fmt - '0');
                    fmt++;
                }
            }
        }

        /* length modifiers (accepted, mapped to 32-bit) */
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z' || *fmt == 'j' || *fmt == 't') {
            fmt++;
        }

        if (width_star) {
            width = va_arg(ap, int);
            if (width < 0) {
                flags |= F_MINUS;
                width = -width;
            }
        }

        char c = *fmt;
        switch (c) {
        case 'd':
        case 'i': {
            int32_t v = va_arg(ap, int32_t);
            int neg = (v < 0);
            uint32_t mag = neg ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
            emit_num(o, mag, 10u, 0, neg, width, prec, flags & ~F_HASH);
            break;
        }
        case 'u':
            emit_num(o, va_arg(ap, uint32_t), 10u, 0, 0, width, prec, flags & ~F_HASH);
            break;
        case 'x':
            emit_num(o, va_arg(ap, uint32_t), 16u, 0, 0, width, prec, flags);
            break;
        case 'X':
            emit_num(o, va_arg(ap, uint32_t), 16u, 1, 0, width, prec, flags);
            break;
        case 'o':
            emit_num(o, va_arg(ap, uint32_t), 8u, 0, 0, width, prec, flags);
            break;
        case 'p':
            out_str(o, "0x", 2);
            emit_num(o, (uint32_t)(uintptr_t)va_arg(ap, void *), 16u, false, false,
                     0, -1, 0);
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (s == NULL) {
                s = "(null)";
            }
            size_t len = strlen(s);
            if (prec >= 0 && (size_t)prec < len) {
                len = (size_t)prec;
            }
            size_t padding = (width > 0 && (size_t)width > len) ? (size_t)width - len : 0u;
            if ((flags & F_MINUS) == 0u) {
                pad(o, ' ', padding);
            }
            out_str(o, s, len);
            if (flags & F_MINUS) {
                pad(o, ' ', padding);
            }
            break;
        }
        case 'c':
            out_ch(o, (char)va_arg(ap, int));
            break;
        case '%':
            out_ch(o, '%');
            break;
        case '\0':
            return (int32_t)o->pos;
        default:
            out_ch(o, '%');
            out_ch(o, c);
            break;
        }
    }
    return (int32_t)o->pos;
}

int32_t sai_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    out_t o = { buf, size, 0u, false };
    if (size == 0u) {
        return 0;
    }
    int32_t n = vsnprintf_impl(&o, fmt, ap);
    if (o.pos < size) {
        buf[o.pos] = '\0';
    } else {
        buf[size - 1u] = '\0';
    }
    return n;
}

int32_t sai_snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int32_t n = sai_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

/* ------------------------------------------------------------------ */
/* Console backend                                                     */
/* ------------------------------------------------------------------ */
#if defined(SAI_HOST_BUILD)
#include <stdio.h>

static void console_putc(char c)
{
    fputc(c, stdout);
}
#else
extern void port_putchar(char c);
static void console_putc(char c)
{
    port_putchar(c);
}
#endif

void sai_console_write(const char *s)
{
    while (*s) {
        if (*s == '\n') {
            console_putc('\r');           /* CRLF on terminals */
        }
        console_putc(*s++);
    }
}

void sai_vprintf(const char *fmt, va_list ap)
{
    char tmp[CONFIG_SAI_CONSOLE_LINE_MAX];
    (void)sai_vsnprintf(tmp, sizeof(tmp), fmt, ap);
    sai_console_write(tmp);
}

void sai_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    sai_vprintf(fmt, ap);
    va_end(ap);
}

void sai_console_init(void)
{
    /* nothing needed: backends are polled and self-contained */
}
