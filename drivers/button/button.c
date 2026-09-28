/**
 * @file drivers/button/button.c
 * @brief Button class: debounced IRQ-driven press/release events.
 *
 * Boards feed raw level changes via sai_button_irq() (ISR context); the
 * driver debounces with a one-shot kernel timer and invokes the subscriber
 * callback plus a press/release event channel for pollers.
 */
#include <sai/devices2.h>
#include <sai/ipc.h>
#include <sai/time.h>
#include <sai/log.h>
#include <string.h>

#define SAI_BUTTON_INSTANCES 2
#define SAI_BUTTON_IDS       4
#define SAI_BUTTON_EVENTS    8

#define BTN_EVT_PRESSED  0x1u
#define BTN_EVT_RELEASED 0x2u

typedef struct {
    sai_device_t dev;
    char         name[10];
    uint32_t     debounce_ms;
    bool         state[SAI_BUTTON_IDS];     /* debounced level */
    sai_button_cb_t cb;
    void        *cb_arg;
    sai_timer_t  timer;                     /* debounce settle timer  */
    uint8_t      pending_id;                /* id being debounced     */
    bool         pending_level;
    bool         timer_armed;
    sai_event_t  events;
    bool         events_init;
} button_dev_t;

static button_dev_t s_buttons[SAI_BUTTON_INSTANCES];

static button_dev_t *button_of(sai_device_t *d)
{
    for (uint32_t i = 0; i < SAI_BUTTON_INSTANCES; i++) {
        if (&s_buttons[i].dev == d) {
            return &s_buttons[i];
        }
    }
    return NULL;
}

/** Debounce timer expired: commit the pending level. */
static void button_settle(void *arg)
{
    button_dev_t *b = (button_dev_t *)arg;
    b->timer_armed = false;
    uint32_t id = b->pending_id;
    if (id >= SAI_BUTTON_IDS) {
        return;
    }
    bool new_level = b->pending_level;
    if (b->state[id] == new_level) {
        return;                             /* glitch settled back */
    }
    b->state[id] = new_level;
    (void)sai_event_set(&b->events, new_level ? BTN_EVT_PRESSED
                                              : BTN_EVT_RELEASED);
    if (b->cb != NULL) {
        b->cb(&b->dev, id, new_level, b->cb_arg);
    }
}

static int button_configure(sai_device_t *dev, uint32_t debounce_ms)
{
    button_dev_t *b = button_of(dev);
    if (b == NULL) {
        return SAI_ERR_INVAL;
    }
    b->debounce_ms = debounce_ms == 0u ? 20u : debounce_ms;
    return SAI_OK;
}

static int button_read(sai_device_t *dev, uint32_t id, bool *pressed)
{
    button_dev_t *b = button_of(dev);
    if (b == NULL || id >= SAI_BUTTON_IDS || pressed == NULL) {
        return SAI_ERR_INVAL;
    }
    *pressed = b->state[id];
    return SAI_OK;
}

static const sai_button_ops_t s_button_ops = {
    .configure = button_configure,
    .read      = button_read,
};

sai_status_t sai_button_irq(sai_device_t *d, uint32_t id, bool raw_level)
{
    button_dev_t *b = button_of(d);
    if (b == NULL || id >= SAI_BUTTON_IDS) {
        return SAI_ERR_INVAL;
    }
    b->pending_id = (uint8_t)id;
    b->pending_level = raw_level;
    if (!b->timer_armed) {
        (void)sai_timer_init(&b->timer, b->name, button_settle, b,
                             b->debounce_ms ? b->debounce_ms : 20u, false);
        (void)sai_timer_start(&b->timer);
        b->timer_armed = true;
    }
    return SAI_OK;
}

sai_status_t sai_button_subscribe(sai_device_t *d, sai_button_cb_t cb, void *arg)
{
    button_dev_t *b = button_of(d);
    if (b == NULL) {
        return SAI_ERR_INVAL;
    }
    b->cb = cb;
    b->cb_arg = arg;
    return SAI_OK;
}

sai_status_t sai_button_create(const char *name, sai_device_t **out)
{
    if (name == NULL || out == NULL) {
        return SAI_ERR_INVAL;
    }
    button_dev_t *b = NULL;
    for (uint32_t i = 0; i < SAI_BUTTON_INSTANCES; i++) {
        if (s_buttons[i].dev.type == SAI_DEVICE_UNKNOWN) {
            b = &s_buttons[i];
            break;
        }
    }
    if (b == NULL) {
        return SAI_ERR_FULL;
    }
    memset(b, 0, sizeof(*b));
    strncpy(b->name, name, sizeof(b->name) - 1u);
    sai_status_t rc = sai_event_init(&b->events, name);
    if (rc != SAI_OK) {
        return rc;
    }
    b->events_init = true;
    rc = sai_device_register(&b->dev, b->name, SAI_DEVICE_TYPE_BUTTON, 0,
                             NULL, 0, (const sai_dev_ops_t *)&s_button_ops,
                             NULL);
    if (rc != SAI_OK) {
        (void)sai_event_destroy(&b->events);
        b->events_init = false;
        return rc;
    }
    *out = &b->dev;
    return SAI_OK;
}

/** Access the press/release event flags of a button device (pollers). */
sai_status_t sai_button_events(sai_device_t *d, sai_event_t **out)
{
    button_dev_t *b = button_of(d);
    if (b == NULL || !b->events_init || out == NULL) {
        return SAI_ERR_INVAL;
    }
    *out = &b->events;
    return SAI_OK;
}
