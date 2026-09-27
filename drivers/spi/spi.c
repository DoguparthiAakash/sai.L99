/**
 * @file drivers/spi/spi.c
 * @brief SPI transaction helper: CS-gated full-duplex transfers.
 *
 * Boards provide the class ops (configure/transceive); this helper adds the
 * chip-select protocol on a GPIO so device drivers do not repeat it.
 */
#include "sai/device.h"
#include "sai/kernel.h"
#include "sai/sync.h"
#include "sai/log.h"

sai_status_t sai_spi_transfer_cs(sai_device_t *spi, sai_device_t *cs_gpio,
                                 uint32_t cs_pin, bool active_low,
                                 const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    if (spi == NULL || cs_gpio == NULL) {
        return SAI_ERR_INVAL;
    }

    (void)sai_gpio_set(cs_gpio, cs_pin, active_low ? false : true);

    int rc = sai_spi_transceive(spi, tx, rx, len);

    (void)sai_gpio_set(cs_gpio, cs_pin, active_low ? true : false);

    return (rc == 0) ? SAI_OK : SAI_ERR_IO;
}
