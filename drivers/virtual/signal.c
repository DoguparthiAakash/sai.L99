/**
 * @file drivers/virtual/signal.c
 * @brief Virtual signal device: a named fan-out point that bridges
 *        "software events" into the device model. Producers raise it from
 *        any context; subscribers run in the subscribing thread via an
 *        event-flags wait, or immediately (ISR context) via callback.
 */
#include <sai/devices2.h>
#include <sai/ipc.h>
#include <sai/log.h>
#include <string.h>

#define SAI_SIGNAL_INSTANCES 4
#define SAI_SIGNAL_MAX_SUBS  4

typedef struct {
    sai_signal_cb_t cb;
    void           *arg;
    bool            used;
} signal_sub_t;

typedef struct {
    sai_device_t   dev;
    char           name[12];
    sai_event_t    ev;
    bool           ev_init;
    signal_sub_t   subs[SAI_SIGNAL_MAX_SUBS];
    volatile uint32_t raised;
} signal_dev_t;

static signal_dev_t s_signals[SAI_SIGNAL_INSTANCES];

static signal_dev_t *signal_of(sai_device_t *d)
{
    for (uint32_t i = 0; i < SAI_SIGNAL_INSTANCES; i++) {
        if (&s_signals[i].dev == d) {
            return &s_signals[i];
        }
    }
    return NULL;
}

static const sai_dev_ops_t s_signal_ops;   /* no class ops; node only */

sai_status_t sai_signal_create(const char *name, sai_device_t **out)
{
    if (name == NULL || out == NULL) {
        return SAI_ERR_INVAL;
    }
    signal_dev_t *s = NULL;
    for (uint32_t i = 0; i < SAI_SIGNAL_INSTANCES; i++) {
        if (s_signals[i].dev.type == SAI_DEVICE_UNKNOWN) {
            s = &s_signals[i];
            break;
        }
    }
    if (s == NULL) {
        return SAI_ERR_FULL;
    }
    memset(s, 0, sizeof(*s));
    strncpy(s->name, name, sizeof(s->name) - 1u);
    sai_status_t rc = sai_event_init(&s->ev, name);
    if (rc != SAI_OK) {
        return rc;
    }
    s->ev_init = true;
    rc = sai_device_register(&s->dev, s->name, SAI_DEVICE_TYPE_SIGNAL,
                             0, NULL, 0, &s_signal_ops, NULL);
    if (rc != SAI_OK) {
        (void)sai_event_destroy(&s->ev);
        s->ev_init = false;
        return rc;
    }
    *out = &s->dev;
    return SAI_OK;
}

/** Raise the signal: sets event flags + fires callbacks (any context). */
sai_status_t sai_signal_raise(sai_device_t *d, uint32_t flags)
{
    signal_dev_t *s = signal_of(d);
    if (s == NULL) {
        return SAI_ERR_INVAL;
    }
    s->raised++;
    (void)sai_isr_event_set(&s->ev, flags == 0u ? 0x1u : flags);
    for (uint32_t i = 0; i < SAI_SIGNAL_MAX_SUBS; i++) {
        if (s->subs[i].used && s->subs[i].cb != NULL) {
            s->subs[i].cb(d, flags, s->subs[i].arg);
        }
    }
    return SAI_OK;
}

/** Subscribe a callback (fires from the raising context). */
sai_status_t sai_signal_subscribe(sai_device_t *d, sai_signal_cb_t cb, void *arg)
{
    signal_dev_t *s = signal_of(d);
    if (s == NULL) {
        return SAI_ERR_INVAL;
    }
    for (uint32_t i = 0; i < SAI_SIGNAL_MAX_SUBS; i++) {
        if (!s->subs[i].used) {
            s->subs[i].cb = cb;
            s->subs[i].arg = arg;
            s->subs[i].used = true;
            return SAI_OK;
        }
    }
    return SAI_ERR_FULL;
}

/** Wait-side: block on the signal's event flags (see sai_event_wait). */
sai_status_t sai_signal_wait(sai_device_t *d, uint32_t flags, uint32_t opts,
                             uint32_t *set_flags, int32_t timeout_ms)
{
    signal_dev_t *s = signal_of(d);
    if (s == NULL || !s->ev_init) {
        return SAI_ERR_INVAL;
    }
    return sai_event_wait(&s->ev, flags == 0u ? 0x1u : flags, opts,
                          set_flags, timeout_ms);
}
