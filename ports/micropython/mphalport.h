/*
 * ports/micropython/mphalport.h
 * MicroPython HAL glue for sai.L99 -- shared by host and target builds.
 * Time comes from the sai kernel tick; IO is routed through the port.
 */
#ifndef SAI_MPHALPORT_H
#define SAI_MPHALPORT_H

#include <stdint.h>
#include "sai/time.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The port implementation (sai_mp_port.c) */
void mp_hal_set_interrupt_char(int c);
int mp_hal_stdin_rx_chr(void);
void mp_hal_stdout_tx_str(const char *str);
void mp_hal_stdout_tx_strn(const char *str, size_t len);
void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len);
void mp_hal_delay_ms(mp_uint_t ms);
void mp_hal_delay_us(mp_uint_t us);
mp_uint_t mp_hal_ticks_ms(void);
mp_uint_t mp_hal_ticks_us(void);
mp_uint_t mp_hal_ticks_cpu(void);
mp_uint64_t mp_hal_time_ns(void);

/* Pins are not modelled by the py core on this port (machine module stubs) */
typedef void *mp_hal_pin_obj_t;
static inline void mp_hal_pin_write(mp_hal_pin_obj_t p, int v) { (void)p; (void)v; }
static inline int mp_hal_pin_read(mp_hal_pin_obj_t p) { (void)p; return 0; }

/* Convenience: kernel sleep in ms (yields to the sai scheduler) */
void sai_mp_sleep_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* SAI_MPHALPORT_H */
