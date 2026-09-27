/**
 * @file drivers/gpio/gpio.c
 * @brief GPIO class helpers: pin registry used by board glue.
 *
 * Boards implement the raw register ops; this module keeps the logical
 * pin-name -> controller/pin mapping (from the devicetree) so applications
 * can sai_gpio_get_by_label("led0").
 */
#include "sai/device.h"
#include "sai/log.h"
#include <string.h>

#define SAI_GPIO_MAX_ENTRIES 32

typedef struct gpio_entry {
    const char   *label;
    sai_device_t *dev;
    uint32_t      pin;
    uint32_t      flags;
} gpio_entry_t;

static gpio_entry_t s_entries[SAI_GPIO_MAX_ENTRIES];
static uint32_t s_entry_count;

sai_status_t sai_gpio_register_pin(const char *label, sai_device_t *dev,
                                   uint32_t pin, uint32_t flags)
{
    if (label == NULL || dev == NULL || s_entry_count >= SAI_GPIO_MAX_ENTRIES) {
        return SAI_ERR_INVAL;
    }
    s_entries[s_entry_count].label = label;
    s_entries[s_entry_count].dev   = dev;
    s_entries[s_entry_count].pin   = pin;
    s_entries[s_entry_count].flags = flags;
    s_entry_count++;
    return SAI_OK;
}

sai_status_t sai_gpio_by_label(const char *label, sai_device_t **dev, uint32_t *pin)
{
    if (label == NULL || dev == NULL || pin == NULL) {
        return SAI_ERR_INVAL;
    }
    for (uint32_t i = 0; i < s_entry_count; i++) {
        if (strcmp(s_entries[i].label, label) == 0) {
            *dev = s_entries[i].dev;
            *pin = s_entries[i].pin;
            return SAI_OK;
        }
    }
    return SAI_ERR_NOENT;
}

/* Convenience wrappers over the class API with configured pin state. */
sai_status_t sai_gpio_pin_set(const char *label, bool value)
{
    sai_device_t *d;
    uint32_t pin;
    sai_status_t rc = sai_gpio_by_label(label, &d, &pin);
    if (rc != SAI_OK) {
        return rc;
    }
    return sai_gpio_set(d, pin, value);
}

sai_status_t sai_gpio_pin_toggle(const char *label)
{
    sai_device_t *d;
    uint32_t pin;
    sai_status_t rc = sai_gpio_by_label(label, &d, &pin);
    if (rc != SAI_OK) {
        return rc;
    }
    return sai_gpio_toggle(d, pin);
}
