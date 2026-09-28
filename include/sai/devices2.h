/**
 * @file sai/devices2.h
 * @brief Second wave of device classes: PWM, button (IRQ), host loopback
 *        and a memory "device" for scratch storage.
 *
 * These classes follow the same ops-in-union pattern as sai/device.h and
 * register through sai_device_register() so they show up in the device
 * enumerator like any UART/GPIO node.
 */
#ifndef SAI_DEVICES2_H
#define SAI_DEVICES2_H

#include <sai/device.h>
#include <sai/ipc.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Class ids (extend sai_device_type_t's numeric space; kept separate to
 * avoid touching the existing enum order in device.h). */
#define SAI_DEVICE_TYPE_PWM     ((sai_device_type_t)100)
#define SAI_DEVICE_TYPE_BUTTON  ((sai_device_type_t)101)
#define SAI_DEVICE_TYPE_LOOPBK  ((sai_device_type_t)102)
#define SAI_DEVICE_TYPE_MEM     ((sai_device_type_t)103)

/* ------------------------------------------------------------------ */
/* PWM class                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    int  (*configure)(sai_device_t *dev, uint32_t period_us);
    int  (*enable)(sai_device_t *dev, uint32_t channel, uint32_t duty_us);
    int  (*disable)(sai_device_t *dev, uint32_t channel);
} sai_pwm_ops_t;

static inline int sai_pwm_configure(sai_device_t *d, uint32_t period_us)
{
    if (d == NULL || d->type != SAI_DEVICE_TYPE_PWM) return SAI_ERR_INVAL;
    return ((const sai_pwm_ops_t *)d->ops)->configure(d, period_us);
}

/** Set duty in microseconds (<= period). duty == 0 disables the channel. */
static inline int sai_pwm_enable(sai_device_t *d, uint32_t ch, uint32_t duty_us)
{
    if (d == NULL || d->type != SAI_DEVICE_TYPE_PWM) return SAI_ERR_INVAL;
    return ((const sai_pwm_ops_t *)d->ops)->enable(d, ch, duty_us);
}

static inline int sai_pwm_disable(sai_device_t *d, uint32_t ch)
{
    if (d == NULL || d->type != SAI_DEVICE_TYPE_PWM) return SAI_ERR_INVAL;
    return ((const sai_pwm_ops_t *)d->ops)->disable(d, ch);
}

/** Create a pooled software-PWM instance ("pwm0", "pwm1", ...). */
sai_status_t sai_pwm_create(const char *name, sai_device_t **out);

/* ------------------------------------------------------------------ */
/* Button class (debounced IRQ-driven)                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    int (*configure)(sai_device_t *dev, uint32_t debounce_ms);
    int (*read)(sai_device_t *dev, uint32_t id, bool *pressed);
} sai_button_ops_t;

/** Press/release callback. Called in ISR context: keep it short. */
typedef void (*sai_button_cb_t)(sai_device_t *dev, uint32_t id, bool pressed,
                                void *arg);

static inline int sai_button_configure(sai_device_t *d, uint32_t debounce_ms)
{
    if (d == NULL || d->type != SAI_DEVICE_TYPE_BUTTON) return SAI_ERR_INVAL;
    return ((const sai_button_ops_t *)d->ops)->configure(d, debounce_ms);
}

static inline int sai_button_read(sai_device_t *d, uint32_t id, bool *pressed)
{
    if (d == NULL || d->type != SAI_DEVICE_TYPE_BUTTON) return SAI_ERR_INVAL;
    return ((const sai_button_ops_t *)d->ops)->read(d, id, pressed);
}

/**
 * Subscribe to press/release events on a button device.  The callback runs
 * in the button driver's ISR context.
 */
sai_status_t sai_button_subscribe(sai_device_t *d, sai_button_cb_t cb, void *arg);

/** Event-flags object of a button device (pollers): bit0 press, bit1 release. */
sai_status_t sai_button_events(sai_device_t *d, sai_event_t **out);

/** Create a pooled button instance ("btn0", "btn1", ...). */
sai_status_t sai_button_create(const char *name, sai_device_t **out);

/** Call from the board's GPIO ISR when the raw line changes. */
sai_status_t sai_button_irq(sai_device_t *d, uint32_t id, bool raw_level);

/* ------------------------------------------------------------------ */
/* Host loopback class (two endpoints; writes appear on the peer)      */
/* ------------------------------------------------------------------ */

typedef struct {
    int  (*write)(sai_device_t *dev, const uint8_t *buf, uint32_t len);
    int  (*read)(sai_device_t *dev, uint8_t *buf, uint32_t len, int32_t timeout_ms);
} sai_loopback_ops_t;

static inline int sai_loopback_write(sai_device_t *d, const uint8_t *buf, uint32_t len)
{
    if (d == NULL || d->type != SAI_DEVICE_TYPE_LOOPBK) return SAI_ERR_INVAL;
    return ((const sai_loopback_ops_t *)d->ops)->write(d, buf, len);
}

/** Read back bytes previously written on the endpoint (or its peer). */
static inline int sai_loopback_read(sai_device_t *d, uint8_t *buf,
                                    uint32_t len, int32_t timeout_ms)
{
    if (d == NULL || d->type != SAI_DEVICE_TYPE_LOOPBK) return SAI_ERR_INVAL;
    return ((const sai_loopback_ops_t *)d->ops)->read(d, buf, len, timeout_ms);
}

/**
 * Create a loopback endpoint pair ("a" and "b").  Either endpoint sees the
 * other's writes.  @p out_a / @p out_b receive the two device nodes.
 */
sai_status_t sai_loopback_create(const char *name_a, const char *name_b,
                                 sai_device_t **out_a, sai_device_t **out_b);

/* ------------------------------------------------------------------ */
/* Memory device (named scratch storage)                               */
/* ------------------------------------------------------------------ */

typedef struct {
    int (*read)(sai_device_t *dev, uint32_t offset, uint8_t *buf, uint32_t len);
    int (*write)(sai_device_t *dev, uint32_t offset, const uint8_t *buf, uint32_t len);
    uint32_t size;
} sai_mem_ops_t;

static inline int sai_mem_read(sai_device_t *d, uint32_t off, uint8_t *b, uint32_t l)
{
    if (d == NULL || d->type != SAI_DEVICE_TYPE_MEM) return SAI_ERR_INVAL;
    return ((const sai_mem_ops_t *)d->ops)->read(d, off, b, l);
}

static inline int sai_mem_write(sai_device_t *d, uint32_t off, const uint8_t *b, uint32_t l)
{
    if (d == NULL || d->type != SAI_DEVICE_TYPE_MEM) return SAI_ERR_INVAL;
    return ((const sai_mem_ops_t *)d->ops)->write(d, off, b, l);
}

/** Create a memory device backed by a caller-owned buffer. */
sai_status_t sai_memdev_create(const char *name, void *storage, uint32_t size,
                               sai_device_t **out);

#ifdef __cplusplus
}
#endif

#endif /* SAI_DEVICES2_H */
