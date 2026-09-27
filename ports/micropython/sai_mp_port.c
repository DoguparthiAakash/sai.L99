/*
 * ports/micropython/sai_mp_port.c
 * MicroPython runtime port for sai.L99: GC heap, stdout/stdin, time.
 *
 * One MicroPython VM runs in its own sai thread.  The GC heap is carved out
 * of a dedicated static arena; stdout goes through sai_console_write (the
 * device console), stdin is polled from the console ring buffer.
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
#include "sai/device.h"
#include "sai/mem.h"

#include "mpconfigport.h"
#include "mphalport.h"

/* ------------------------------------------------------------------ */
/* GC heap                                                             */
/* ------------------------------------------------------------------ */
#if defined(SAI_HOST_BUILD)
#define MP_HEAP_SIZE (1u * 1024u * 1024u)
#else
#define MP_HEAP_SIZE (64u * 1024u)
#endif

#ifndef SAI_HOST_BUILD
/* Target: the VM heap is a static arena inside the kernel heap region. */
static uint8_t mp_heap[MP_HEAP_SIZE] __attribute__((aligned(8)));
#endif

void sai_mp_init(void)
{
#ifdef SAI_HOST_BUILD
    static uint8_t heap[MP_HEAP_SIZE] __attribute__((aligned(8)));
    gc_init(heap, heap + MP_HEAP_SIZE);
#else
    gc_init(mp_heap, mp_heap + MP_HEAP_SIZE);
#endif
    mp_init();
}

void sai_mp_deinit(void)
{
    mp_deinit();
}

/* ------------------------------------------------------------------ */
/* stdout / stderr                                                     */
/* ------------------------------------------------------------------ */
/* Used by mp_plat_print (declared in py/mpconfig.h as extern struct). */
const struct _mp_print_t mp_plat_print;

static void mp_stdout_emit(const char *str, size_t len)
{
    sai_console_write(str, len);
}

void mp_hal_stdout_tx_str(const char *str)
{
    mp_stdout_emit(str, strlen(str));
}

void mp_hal_stdout_tx_strn(const char *str, size_t len)
{
    mp_stdout_emit(str, len);
}

void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len)
{
    /* \n -> \r\n on the wire (console is a raw UART stream) */
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '\n') {
            mp_stdout_emit("\r", 1);
        }
        mp_stdout_emit(&str[i], 1);
    }
}

/* ------------------------------------------------------------------ */
/* stdin (non-blocking poll + blocking char)                           */
/* ------------------------------------------------------------------ */
int mp_hal_stdin_rx_chr(void)
{
    for (;;) {
        uint8_t c;
        if (sai_console_read(&c, 1) == 1) {
            return (int)c;
        }
        sai_mp_sleep_ms(2);
    }
}

/* ------------------------------------------------------------------ */
/* time                                                                */
/* ------------------------------------------------------------------ */
void mp_hal_delay_ms(mp_uint_t ms)
{
    sai_mp_sleep_ms(ms);
}

void mp_hal_delay_us(mp_uint_t us)
{
    if (us < 1000u) {
        sai_busy_wait_us(us);
    } else {
        sai_mp_sleep_ms(us / 1000u);
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

/* ------------------------------------------------------------------ */
/* misc required callbacks                                             */
/* ------------------------------------------------------------------ */
void mp_hal_set_interrupt_char(int c)
{
    (void)c;    /* no soft IRQ char on this port */
}

NORETURN void nlr_jump_fail(void *val)
{
    (void)val;
    sai_printf("MP: nlr_jump_fail -- halting\n");
    port_halt();
    for (;;) {
    }
}

#if defined(NDEBUG) && defined(SAI_HOST_BUILD)
void __assert_func(const char *file, int line, const char *func, const char *expr)
{
    (void)file; (void)line; (void)func; (void)expr;
    for (;;) {
    }
}
#endif

/* ------------------------------------------------------------------ */
/* Script execution (used by the scripting service)                    */
/* ------------------------------------------------------------------ */
static const char evaluated_from[] = "<script>";

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
    (void)evaluated_from;
    return SAI_ERR_INVAL;
}
