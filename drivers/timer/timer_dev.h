/** @file drivers/timer/timer_dev.h @brief Timer device helper API. */
#ifndef SAI_DRIVERS_TIMER_DEV_H
#define SAI_DRIVERS_TIMER_DEV_H

#include <sai/device.h>
#include <sai/time.h>

sai_status_t sai_timer_device_open(sai_device_t *dev, uint32_t idx,
                                   sai_timer_fn_t fn, void *arg);
sai_status_t sai_timer_device_start(sai_device_t *dev, uint32_t period_ms, bool periodic);
sai_status_t sai_timer_device_stop(sai_device_t *dev);

#endif
