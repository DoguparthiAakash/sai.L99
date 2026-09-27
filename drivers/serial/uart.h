/** @file drivers/serial/uart.h @brief UART class helper API. */
#ifndef SAI_DRIVERS_UART_H
#define SAI_DRIVERS_UART_H

#include <sai/device.h>

sai_status_t sai_uart_attach_receiver(sai_device_t *dev);
sai_status_t sai_uart_rx_push(sai_device_t *dev, uint8_t byte);
int32_t      sai_uart_read(sai_device_t *dev, void *buf, uint32_t len, int32_t timeout_ms);
int32_t      sai_uart_write(sai_device_t *dev, const void *buf, uint32_t len);

#endif
