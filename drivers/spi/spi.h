/** @file drivers/spi/spi.h @brief SPI class helper API. */
#ifndef SAI_DRIVERS_SPI_H
#define SAI_DRIVERS_SPI_H

#include <sai/device.h>

sai_status_t sai_spi_transfer_cs(sai_device_t *spi, sai_device_t *cs_gpio,
                                 uint32_t cs_pin, bool active_low,
                                 const uint8_t *tx, uint8_t *rx, uint32_t len);

#endif
