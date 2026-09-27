/**
 * @file kernel/device.c
 * @brief Device framework registry + UART convenience helper.
 */
#include "internal.h"
#include <sai/device.h>

static sai_device_t *s_devices = NULL;
static uint32_t s_device_count = 0;

sai_status_t sai_device_register(sai_device_t *dev, const char *name,
                                 sai_device_type_t type, uint32_t dt_node,
                                 void *regs, uint32_t irq,
                                 const sai_dev_ops_t *ops, void *data)
{
    if (dev == NULL || name == NULL || ops == NULL) {
        return SAI_ERR_INVAL;
    }
    dev->kobj.type = SAI_KOBJ_DEVICE;
    dev->kobj.name = name;
    dev->name    = name;
    dev->type    = type;
    dev->dt_node = dt_node;
    dev->regs    = regs;
    dev->irq     = irq;
    dev->ops     = ops;
    dev->data    = data;

    uint32_t key = port_lock();
    dev->next = s_devices;
    s_devices = dev;
    s_device_count++;
    port_unlock(key);
    return SAI_OK;
}

sai_device_t *sai_device_get_by_node(uint32_t dt_node)
{
    for (sai_device_t *d = s_devices; d != NULL; d = d->next) {
        if (d->dt_node == dt_node) {
            return d;
        }
    }
    return NULL;
}

sai_device_t *sai_device_iter(sai_device_t *prev)
{
    return (prev == NULL) ? s_devices : prev->next;
}

uint32_t sai_device_count(void)
{
    return s_device_count;
}

void sai_uart_puts(sai_device_t *d, const char *s)
{
    if (d == NULL || d->ops == NULL) {
        return;
    }
    while (*s) {
        if (d->ops->uart.poll_out(d, *s) != 0) {
            break;
        }
        s++;
    }
}
