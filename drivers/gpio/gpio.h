/** @file drivers/gpio/gpio.h @brief GPIO logical-pin helper API. */
#ifndef SAI_DRIVERS_GPIO_H
#define SAI_DRIVERS_GPIO_H

#include <sai/device.h>

sai_status_t sai_gpio_register_pin(const char *label, sai_device_t *dev,
                                   uint32_t pin, uint32_t flags);
sai_status_t sai_gpio_by_label(const char *label, sai_device_t **dev, uint32_t *pin);
sai_status_t sai_gpio_pin_set(const char *label, bool value);
sai_status_t sai_gpio_pin_toggle(const char *label);

#endif
