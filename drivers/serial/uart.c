/**
 * @file drivers/serial/uart.c
 * @brief Generic UART class helper: ISR-fed RX ring buffer + polled TX.
 *
 * SoC drivers (see the board BSP under boards/) provide register-level
 * poll_out and call sai_uart_rx_push() from their RX ISR; threads then read
 * through sai_uart_read() which blocks on the attached mailbox.
 */
#include "sai/device.h"
#include "sai/ringbuf.h"
#include "sai/kernel.h"
#include "sai/ipc.h"
#include "sai/log.h"

#define UART_RX_BUF_SIZE 128u   /* power of two */

typedef struct uart_rx_ctx {
    sai_ringbuf_t ring;
    uint8_t       storage[UART_RX_BUF_SIZE];
    sai_mbox_t    notify;      /* byte-count mailbox: readers block here */
    bool          notify_init;
} uart_rx_ctx_t;

static uart_rx_ctx_t s_uart_ctx[4];
static uint32_t s_uart_ctx_count;

uart_rx_ctx_t *uart_rx_ctx_get(sai_device_t *dev)
{
    /* Contexts are assigned in registration order; board code registers
     * consoles first. For a production build these would live in dev->data. */
    for (uint32_t i = 0; i < s_uart_ctx_count; i++) {
        if (s_uart_ctx[i].notify_init) {
            return &s_uart_ctx[i];
        }
    }
    return NULL;
}

sai_status_t sai_uart_attach_receiver(sai_device_t *dev)
{
    if (dev == NULL || s_uart_ctx_count >= 4u) {
        return SAI_ERR_INVAL;
    }
    uart_rx_ctx_t *ctx = &s_uart_ctx[s_uart_ctx_count++];
    sai_ringbuf_init(&ctx->ring, ctx->storage, UART_RX_BUF_SIZE);
    sai_status_t rc = sai_mbox_init(&ctx->notify, "uart_notify", NULL, UART_RX_BUF_SIZE);
    if (rc != SAI_OK) {
        s_uart_ctx_count--;
        return rc;
    }
    ctx->notify_init = true;
    dev->data = ctx;
    return SAI_OK;
}

/** Called from the RX ISR with the received byte. Never blocks. */
sai_status_t sai_uart_rx_push(sai_device_t *dev, uint8_t byte)
{
    uart_rx_ctx_t *ctx = (uart_rx_ctx_t *)dev->data;
    if (ctx == NULL) {
        return SAI_ERR_NOINIT;
    }
    if (!sai_ringbuf_put(&ctx->ring, byte)) {
        return SAI_ERR_FULL;             /* dropped: ring full */
    }
    return sai_isr_mbox_put(&ctx->notify, 1u);   /* wake one reader */
}

int32_t sai_uart_read(sai_device_t *dev, void *buf, uint32_t len, int32_t timeout_ms)
{
    uart_rx_ctx_t *ctx = (uart_rx_ctx_t *)dev->data;
    if (ctx == NULL) {
        return SAI_ERR_NOINIT;
    }
    uint8_t *out = buf;
    uint32_t got = 0;
    while (got < len) {
        uint8_t b;
        if (sai_ringbuf_get(&ctx->ring, &b)) {
            out[got++] = b;
            continue;
        }
        if (got > 0) {
            break;                       /* partial read is valid */
        }
        /* block until ISR feeds us */
        uint8_t tok;
        if (sai_mbox_get(&ctx->notify, &tok, timeout_ms) != SAI_OK) {
            break;
        }
    }
    return (int32_t)got;
}

int32_t sai_uart_write(sai_device_t *dev, const void *buf, uint32_t len)
{
    const char *s = buf;
    for (uint32_t i = 0; i < len; i++) {
        sai_uart_poll_out(dev, s[i]);    /* polled TX: CR/LF translation */
        if (s[i] == '\n') {
            sai_uart_poll_out(dev, '\r');
        }
    }
    return (int32_t)len;
}
