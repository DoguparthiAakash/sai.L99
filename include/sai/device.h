/**
 * @file sai/device.h
 * @brief Driver framework: device nodes, buses, class APIs (uart/gpio/spi/i2c/timer).
 *
 * A device is described in the devicetree (see tools/dts2h.py) and bound at
 * boot. Drivers implement a class ops table; applications talk to devices
 * through the class API (sai_uart_* etc.), never through driver internals.
 */
#ifndef SAI_DEVICE_H
#define SAI_DEVICE_H

#include <sai/types.h>
#include <sai/config.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Device nodes                                                        */
/* ------------------------------------------------------------------ */
typedef enum {
    SAI_DEVICE_UNKNOWN = 0,
    SAI_DEVICE_UART,
    SAI_DEVICE_GPIO,
    SAI_DEVICE_SPI,
    SAI_DEVICE_I2C,
    SAI_DEVICE_TIMER,
    SAI_DEVICE_COUNTER,
    SAI_DEVICE_ADC,
    SAI_DEVICE_DMA,
} sai_device_type_t;

struct sai_device;

/** ADC async completion: @p value is the averaged sample (ISR context). */
typedef void (*sai_adc_cb_t)(struct sai_device *dev, uint32_t channel,
                             uint16_t value, void *arg);

/** DMA transfer completion (ISR context): @p len bytes were moved. */
typedef void (*sai_dma_cb_t)(struct sai_device *dev, uint32_t channel,
                             uint32_t len, int status, void *arg);

/** Per-class operations; drivers provide one of these. */
typedef union sai_dev_ops {
    struct {
        int (*poll_out)(struct sai_device *dev, char c);
        int (*poll_in)(struct sai_device *dev, char *c);
    } uart;
    struct {
        int (*configure)(struct sai_device *dev, uint32_t pin, uint32_t flags);
        int (*set)(struct sai_device *dev, uint32_t pin, bool value);
        int (*get)(struct sai_device *dev, uint32_t pin, bool *value);
        int (*toggle)(struct sai_device *dev, uint32_t pin);
    } gpio;
    struct {
        int (*configure)(struct sai_device *dev, uint32_t speed_hz, uint8_t mode);
        int (*transceive)(struct sai_device *dev,
                          const uint8_t *tx, uint8_t *rx, uint32_t len);
    } spi;
    struct {
        int (*configure)(struct sai_device *dev, uint32_t speed_hz);
        int (*write)(struct sai_device *dev, uint16_t addr,
                     const uint8_t *buf, uint32_t len);
        int (*read)(struct sai_device *dev, uint16_t addr,
                    uint8_t *buf, uint32_t len);
    } i2c;
    struct {
        int (*start)(struct sai_device *dev);
        int (*stop)(struct sai_device *dev);
        int (*set_period)(struct sai_device *dev, uint32_t period_us);
    } timer;
    struct {
        int (*configure)(struct sai_device *dev, uint32_t channel,
                         uint32_t sample_us, uint8_t samples);
        int (*read)(struct sai_device *dev, uint32_t channel, uint16_t *out);
        int (*read_async)(struct sai_device *dev, uint32_t channel,
                          sai_adc_cb_t cb, void *arg);
    } adc;
    struct {
        int (*configure)(struct sai_device *dev, uint32_t channel,
                         uint32_t burst_size);
        int (*start)(struct sai_device *dev, uint32_t channel,
                     void *dst, const void *src, uint32_t len,
                     sai_dma_cb_t cb, void *arg);
        int (*stop)(struct sai_device *dev, uint32_t channel);
    } dma;
    struct {
        int (*write)(struct sai_device *dev, const uint8_t *buf, uint32_t len);
    } sink;
} sai_dev_ops_t;

/** A device node: created statically by the board (from the devicetree). */
typedef struct sai_device {
    sai_kobj_t          kobj;
    const char         *name;       /**< Node name (from dts).               */
    sai_device_type_t   type;       /**< Class.                              */
    uint32_t            dt_node;   /**< Devicetree node id.                 */
    void               *regs;       /**< MMIO base (from dts reg property).  */
    uint32_t            irq;        /**< Interrupt line.                     */
    const sai_dev_ops_t *ops;      /**< Class operations.                   */
    void               *data;       /**< Driver instance data.               */
    struct sai_device  *next;       /**< Device list linkage.                */
} sai_device_t;

/**
 * Initialize a device node and register it with the framework.
 * Call from board code (see the BSP under boards/).
 */
sai_status_t sai_device_register(sai_device_t *dev, const char *name,
                                 sai_device_type_t type, uint32_t dt_node,
                                 void *regs, uint32_t irq,
                                 const sai_dev_ops_t *ops, void *data);

/** Look up a device by (optional) devicetree node id. */
sai_device_t *sai_device_get_by_node(uint32_t dt_node);

/** Iterate over all registered devices; returns NULL at end. */
sai_device_t *sai_device_iter(sai_device_t *prev);

uint32_t sai_device_count(void);

/* ------------------------------------------------------------------ */
/* GPIO class API                                                      */
/* ------------------------------------------------------------------ */
#define SAI_GPIO_OUTPUT   0x01u
#define SAI_GPIO_INPUT    0x02u
#define SAI_GPIO_PULL_UP  0x10u
#define SAI_GPIO_PULL_DOWN 0x20u
#define SAI_GPIO_ACTIVE_LOW 0x40u

static inline int sai_gpio_configure(sai_device_t *d, uint32_t pin, uint32_t f)
{ return d && d->ops ? d->ops->gpio.configure(d, pin, f) : SAI_ERR_INVAL; }
static inline int sai_gpio_set(sai_device_t *d, uint32_t pin, bool v)
{ return d && d->ops ? d->ops->gpio.set(d, pin, v) : SAI_ERR_INVAL; }
static inline int sai_gpio_get(sai_device_t *d, uint32_t pin, bool *v)
{ return d && d->ops ? d->ops->gpio.get(d, pin, v) : SAI_ERR_INVAL; }
static inline int sai_gpio_toggle(sai_device_t *d, uint32_t pin)
{ return d && d->ops ? d->ops->gpio.toggle(d, pin) : SAI_ERR_INVAL; }

/* ------------------------------------------------------------------ */
/* UART class API                                                      */
/* ------------------------------------------------------------------ */
static inline void sai_uart_poll_out(sai_device_t *d, char c)
{ if (d && d->ops) { d->ops->uart.poll_out(d, c); } }

static inline int sai_uart_poll_in(sai_device_t *d, char *c)
{ return d && d->ops ? d->ops->uart.poll_in(d, c) : SAI_ERR_INVAL; }

/** Convenience: write a NUL-terminated string (polled). */
void sai_uart_puts(sai_device_t *d, const char *s);

/* ------------------------------------------------------------------ */
/* SPI class API                                                       */
/* ------------------------------------------------------------------ */
static inline int sai_spi_configure(sai_device_t *d, uint32_t hz, uint8_t mode)
{ return d && d->ops ? d->ops->spi.configure(d, hz, mode) : SAI_ERR_INVAL; }
static inline int sai_spi_transceive(sai_device_t *d, const uint8_t *tx,
                                     uint8_t *rx, uint32_t len)
{ return d && d->ops ? d->ops->spi.transceive(d, tx, rx, len) : SAI_ERR_INVAL; }

/* ------------------------------------------------------------------ */
/* I2C class API                                                       */
/* ------------------------------------------------------------------ */
static inline int sai_i2c_configure(sai_device_t *d, uint32_t hz)
{ return d && d->ops ? d->ops->i2c.configure(d, hz) : SAI_ERR_INVAL; }
static inline int sai_i2c_write(sai_device_t *d, uint16_t addr,
                                const uint8_t *buf, uint32_t len)
{ return d && d->ops ? d->ops->i2c.write(d, addr, buf, len) : SAI_ERR_INVAL; }
static inline int sai_i2c_read(sai_device_t *d, uint16_t addr,
                               uint8_t *buf, uint32_t len)
{ return d && d->ops ? d->ops->i2c.read(d, addr, buf, len) : SAI_ERR_INVAL; }

/* ------------------------------------------------------------------ */
/* Timer class API                                                     */
/* ------------------------------------------------------------------ */
static inline int sai_timer_start_dev(sai_device_t *d)
{ return d && d->ops ? d->ops->timer.start(d) : SAI_ERR_INVAL; }
static inline int sai_timer_stop_dev(sai_device_t *d)
{ return d && d->ops ? d->ops->timer.stop(d) : SAI_ERR_INVAL; }
static inline int sai_timer_set_period(sai_device_t *d, uint32_t us)
{ return d && d->ops ? d->ops->timer.set_period(d, us) : SAI_ERR_INVAL; }

/* ------------------------------------------------------------------ */
/* ADC class API                                                       */
/* ------------------------------------------------------------------ */

/** Configure a channel: sample interval in us, 1..16 samples averaged. */
static inline int sai_adc_configure(sai_device_t *d, uint32_t channel,
                                    uint32_t sample_us, uint8_t samples)
{
    if (d == NULL || d->type != SAI_DEVICE_ADC) return SAI_ERR_INVAL;
    return d->ops->adc.configure(d, channel, sample_us, samples);
}

/** Blocking single conversion (averaged). Returns 12-bit value via @p out. */
static inline int sai_adc_read(sai_device_t *d, uint32_t channel, uint16_t *out)
{
    if (d == NULL || d->type != SAI_DEVICE_ADC) return SAI_ERR_INVAL;
    return d->ops->adc.read(d, channel, out);
}

/** Non-blocking conversion; @p cb fires from tick context when ready. */
static inline int sai_adc_read_async(sai_device_t *d, uint32_t channel,
                                     sai_adc_cb_t cb, void *arg)
{
    if (d == NULL || d->type != SAI_DEVICE_ADC) return SAI_ERR_INVAL;
    return d->ops->adc.read_async(d, channel, cb, arg);
}

/* ------------------------------------------------------------------ */
/* DMA class API                                                       */
/* ------------------------------------------------------------------ */

/** Configure a channel's burst size (bytes per arbitration). */
static inline int sai_dma_configure(sai_device_t *d, uint32_t channel,
                                    uint32_t burst_size)
{
    if (d == NULL || d->type != SAI_DEVICE_DMA) return SAI_ERR_INVAL;
    return d->ops->dma.configure(d, channel, burst_size);
}

/**
 * Start a transfer. @p cb fires from tick context on completion (status
 * SAI_OK) or error. Returns SAI_ERR_BUSY if the channel is active.
 */
static inline int sai_dma_start(sai_device_t *d, uint32_t channel,
                                void *dst, const void *src, uint32_t len,
                                sai_dma_cb_t cb, void *arg)
{
    if (d == NULL || d->type != SAI_DEVICE_DMA) return SAI_ERR_INVAL;
    return d->ops->dma.start(d, channel, dst, src, len, cb, arg);
}

/** Abort an active channel transfer. */
static inline int sai_dma_stop(sai_device_t *d, uint32_t channel)
{
    if (d == NULL || d->type != SAI_DEVICE_DMA) return SAI_ERR_INVAL;
    return d->ops->dma.stop(d, channel);
}

#ifdef __cplusplus
}
#endif

#endif /* SAI_DEVICE_H */
