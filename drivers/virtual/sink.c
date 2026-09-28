/**
 * @file drivers/virtual/sink.c
 * @brief Virtual byte-sink device: a write-only capturing endpoint.
 *        Everything written is stored in a static ring (and optionally
 *        mirrored to a callback) — the receive half for loopback-style
 *        tests and a route for console/logging output.
 */
#include <sai/devices2.h>
#include <sai/ringbuf.h>
#include <sai/log.h>
#include <string.h>

#define SAI_SINK_INSTANCES 2
#define SAI_SINK_BUF       256

typedef struct {
    sai_device_t   dev;
    char           name[12];
    sai_ringbuf_t  ring;
    uint8_t        storage[SAI_SINK_BUF];
    sai_sink_cb_t  mirror;
    volatile uint32_t written;
    bool           used;
} sink_dev_t;

static sink_dev_t s_sinks[SAI_SINK_INSTANCES];

static sink_dev_t *sink_of(sai_device_t *d)
{
    for (uint32_t i = 0; i < SAI_SINK_INSTANCES; i++) {
        if (&s_sinks[i].dev == d) {
            return &s_sinks[i];
        }
    }
    return NULL;
}

static int sink_write(sai_device_t *dev, const uint8_t *buf, uint32_t len)
{
    sink_dev_t *s = sink_of(dev);
    if (s == NULL || buf == NULL) {
        return SAI_ERR_INVAL;
    }
    uint32_t queued = 0;
    for (uint32_t i = 0; i < len; i++) {
        if (!sai_ringbuf_put(&s->ring, buf[i])) {
            break;                       /* drop when full (test device) */
        }
        queued++;
    }
    s->written += queued;
    if (s->mirror != NULL) {
        s->mirror(dev, buf, queued, NULL);
    }
    return (int)queued;
}

static const sai_dev_ops_t s_sink_ops = {
    .sink = {
        .write = sink_write,
    },
};

sai_status_t sai_sink_create(const char *name, sai_device_t **out)
{
    if (name == NULL || out == NULL) {
        return SAI_ERR_INVAL;
    }
    sink_dev_t *s = NULL;
    for (uint32_t i = 0; i < SAI_SINK_INSTANCES; i++) {
        if (!s_sinks[i].used) {
            s = &s_sinks[i];
            break;
        }
    }
    if (s == NULL) {
        return SAI_ERR_FULL;
    }
    memset(s, 0, sizeof(*s));
    strncpy(s->name, name, sizeof(s->name) - 1u);
    sai_ringbuf_init(&s->ring, s->storage, SAI_SINK_BUF);
    sai_status_t rc = sai_device_register(&s->dev, s->name,
                                          SAI_DEVICE_TYPE_SINK,
                                          0, NULL, 0, &s_sink_ops, NULL);
    if (rc != SAI_OK) {
        return rc;
    }
    s->used = true;
    *out = &s->dev;
    return SAI_OK;
}

/** Read back captured bytes (drains the ring). */
int32_t sai_sink_read(sai_device_t *d, uint8_t *buf, uint32_t len)
{
    sink_dev_t *s = sink_of(d);
    if (s == NULL || buf == NULL) {
        return SAI_ERR_INVAL;
    }
    uint32_t got = 0;
    while (got < len && sai_ringbuf_get(&s->ring, &buf[got])) {
        got++;
    }
    return (int32_t)got;
}

/** Mirror written bytes to a callback (e.g. route to the console). */
sai_status_t sai_sink_mirror(sai_device_t *d, sai_sink_cb_t cb)
{
    sink_dev_t *s = sink_of(d);
    if (s == NULL) {
        return SAI_ERR_INVAL;
    }
    s->mirror = cb;
    return SAI_OK;
}

/** Bytes written since creation (drop counter: written - captured). */
uint32_t sai_sink_written(sai_device_t *d)
{
    sink_dev_t *s = sink_of(d);
    return s == NULL ? 0u : s->written;
}
