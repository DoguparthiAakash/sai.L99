/**
 * @file drivers/i2c/i2c.c
 * @brief I2C helpers: register-oriented transactions on the class API.
 */
#include "sai/device.h"
#include "sai/log.h"

sai_status_t sai_i2c_write_reg(sai_device_t *bus, uint16_t addr,
                               uint8_t reg, const uint8_t *data, uint32_t len)
{
    if (bus == NULL || data == NULL || len == 0u || len > 254u) {
        return SAI_ERR_INVAL;
    }
    uint8_t buf[255];
    buf[0] = reg;
    for (uint32_t i = 0; i < len; i++) {
        buf[1u + i] = data[i];
    }
    int rc = sai_i2c_write(bus, addr, buf, len + 1u);
    return (rc == 0) ? SAI_OK : SAI_ERR_IO;
}

sai_status_t sai_i2c_read_reg(sai_device_t *bus, uint16_t addr,
                              uint8_t reg, uint8_t *data, uint32_t len)
{
    if (bus == NULL || data == NULL || len == 0u) {
        return SAI_ERR_INVAL;
    }
    int rc = sai_i2c_write(bus, addr, &reg, 1u);
    if (rc != 0) {
        return SAI_ERR_IO;
    }
    rc = sai_i2c_read(bus, addr, data, len);
    return (rc == 0) ? SAI_OK : SAI_ERR_IO;
}
