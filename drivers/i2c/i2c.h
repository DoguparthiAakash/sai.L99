/** @file drivers/i2c/i2c.h @brief I2C class helper API. */
#ifndef SAI_DRIVERS_I2C_H
#define SAI_DRIVERS_I2C_H

#include <sai/device.h>

sai_status_t sai_i2c_write_reg(sai_device_t *bus, uint16_t addr,
                               uint8_t reg, const uint8_t *data, uint32_t len);
sai_status_t sai_i2c_read_reg(sai_device_t *bus, uint16_t addr,
                              uint8_t reg, uint8_t *data, uint32_t len);

#endif
