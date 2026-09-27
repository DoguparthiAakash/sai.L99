/*
 * ports/micropython/sai_mp_port.c
 * MicroPython runtime port for sai.L99: GC heap, stdout/stdin, time.
 *
 * One MicroPython VM runs in its own sai thread.  The GC heap is a dedicated
 * static arena (1 MB on host, 64 KB on target).  stdout is chunked through
 * the sai console (2 KB chunks, no intermediate heap copy); stdin is a
 * message queue fed by the console driver's RX path (or tests).
 */
#include <string.h>
#include <stdio.h>

#include "py/mpconfig.h"
#include "py/mpstate.h"
#include "py/nlr.h"
#include "py/compile.h"
#include "py/runtime.h"
#include "py/gc.h"
#include "py/repl.h"

#include "sai/kernel.h"
#include "sai/time.h"
#include "sai/console.h"
#include "sai/ipc.h"

#include "mpconfigport.h"
#include "mphalport.h"

/* ------------------------------------------------------------------ */
/* GC heap                                                             */
/* ------------------------------------------------------------------ */
#ifdef SAI_HOST_BUILD
#define MP_HEAP_SIZE (1u * 1024u * 1024u)
#else
/* 48 KB fits the F407's 128 KB SRAM next to the (reduced) kernel heap. */
#define MP_HEAP_SIZE (48u * 1024u)
#endif
/* uint64_t array => 8-aligned on every toolchain (MSVC has no __attribute__). */
static uint64_t mp_heap_store[(MP_HEAP_SIZE + 7u) / 8u];
#define mp_heap ((uint8_t *)mp_heap_store)

void sai_mp_init(void)
{
    gc_init(mp_heap, mp_heap + MP_HEAP_SIZE);
    mp_init();
}

void sai_mp_deinit(void)
{
    mp_deinit();
}

/* ------------------------------------------------------------------ */
/* stdout: chunked through the sai console                             */
/* ------------------------------------------------------------------ */
enum { MP_OUT_CHUNK = 2048 };

/* The print object the py core declares as `extern const mp_print_t
 * mp_plat_print`; route it to the console.  (Name must not collide with
 * MP's real mp_print_strn API.) */
static void sai_mp_out(void *data, const char *str, size_t len)
{
    (void)data;
    while (len > 0) {
        size_t n = len > MP_OUT_CHUNK ? MP_OUT_CHUNK : len;
        char buf[MP_OUT_CHUNK];
        memcpy(buf, str, n);
        buf[n] = '\0';
        sai_console_write(buf);
        str += n;
        len -= n;
    }
}

const mp_print_t mp_plat_print = {
    .data = NULL,
    .print_strn = sai_mp_out,
};

void mp_hal_stdout_tx_str(const char *str)
{
    sai_mp_out(NULL, str, strlen(str));
}

void mp_hal_stdout_tx_strn(const char *str, size_t len)
{
    sai_mp_out(NULL, str, len);
}

void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len)
{
    /* \n -> \r\n on the wire (console is a raw UART stream) */
    size_t i = 0;
    while (i < len) {
        size_t n = len - i;
        if (n > MP_OUT_CHUNK - 2) {
            n = MP_OUT_CHUNK - 2;
        }
        char buf[MP_OUT_CHUNK];
        size_t o = 0;
        for (size_t j = 0; j < n; j++) {
            if (str[i + j] == '\n') {
                buf[o++] = '\r';
            }
            buf[o++] = str[i + j];
        }
        buf[o] = '\0';
        sai_console_write(buf);
        i += n;
    }
}

/* ------------------------------------------------------------------ */
/* stdin: message queue fed by the console RX path (or tests)          */
/* ------------------------------------------------------------------ */
static sai_msgq_t s_stdin_q;
static uint32_t s_stdin_pool[16];      /* 16 * 4-byte char slots          */
static bool s_stdin_q_ready;

void sai_mp_stdin_feed(char c)
{
    if (!s_stdin_q_ready) {
        return;
    }
    (void)sai_msgq_put(&s_stdin_q, &c, SAI_NO_WAIT);
}

int mp_hal_stdin_rx_chr(void)
{
    char c;
    while (sai_msgq_get(&s_stdin_q, &c, SAI_WAIT_FOREVER) != SAI_OK) {
        sai_sleep(2);
    }
    return (int)(uint8_t)c;
}

/* ------------------------------------------------------------------ */
/* time                                                                */
/* ------------------------------------------------------------------ */
void mp_hal_delay_ms(mp_uint_t ms)
{
    if (ms == 0u) {
        sai_yield();
        return;
    }
    sai_sleep(ms);
}

void mp_hal_delay_us(mp_uint_t us)
{
    if (us < 1000u) {
        sai_busy_wait_us(us);
    } else {
        sai_sleep(us / 1000u);
    }
}

mp_uint_t mp_hal_ticks_ms(void)
{
    return (mp_uint_t)sai_tick_count();
}

mp_uint_t mp_hal_ticks_us(void)
{
    return (mp_uint_t)(sai_tick_count() * 1000u);
}

mp_uint_t mp_hal_ticks_cpu(void)
{
    return mp_hal_ticks_us();
}

mp_uint64_t mp_hal_time_ns(void)
{
    return (mp_uint64_t)sai_tick_count() * 1000000ull;
}

void mp_hal_set_interrupt_char(int c)
{
    (void)c;    /* no soft IRQ char on this port */
}

/* ------------------------------------------------------------------ */
/* required callbacks                                                  */
/* ------------------------------------------------------------------ */
NORETURN void nlr_jump_fail(void *val)
{
    (void)val;
    sai_printf("MP: nlr_jump_fail -- halting\n");
    port_halt();
}

#if defined(NDEBUG) && defined(SAI_HOST_BUILD)
void __assert_func(const char *file, int line, const char *func, const char *expr)
{
    (void)file; (void)line; (void)func; (void)expr;
    sai_printf("MP assert %s:%d %s %s\n", file, line, func, expr);
    port_halt();
}
#endif

/* ------------------------------------------------------------------ */
/* Script execution (used by the scripting service)                    */
/* ------------------------------------------------------------------ */
sai_status_t sai_mp_exec(const char *src, size_t len)
{
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(
            MP_QSTR__lt_stdin_gt_, src, len, 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_obj_t module_fun = mp_compile(&parse_tree, source_name, true);
        mp_call_function_0(module_fun);
        nlr_pop();
        return SAI_OK;
    }
    /* Uncaught Python exception: report it and return an error. */
    mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
    return SAI_ERR_INVAL;
}
