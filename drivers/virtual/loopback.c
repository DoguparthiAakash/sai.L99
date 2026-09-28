/**
 * @file drivers/virtual/loopback.c
 * @brief Host/test loopback class: a pair of endpoints; bytes written to
 *        either endpoint appear on the other (and on itself if unpaired).
 *
 * Implements a two-slot pattern used by the shell line protocol tests and
 * by MicroPython stdin wiring: the "console" side writes, a service thread
 * reads on the peer endpoint with a timeout.
 */
#include <sai/devices2.h>
#include <sai/ringbuf.h>
#include <sai/ipc.h>
#include <sai/log.h>
#include <string.h>

#define SAI_LOOPBACK_PAIRS 2
#define SAI_LOOPBACK_BUF   128

typedef struct loopback_end {
    sai_device_t  dev;
    char          name[12];
    sai_ringbuf_t ring;
    uint8_t       storage[SAI_LOOPBACK_BUF];
    sai_mbox_t    notify;               /* bytes-available mailbox       */
    bool          notify_init;
    struct loopback_end *peer;
    bool          used;
} loopback_end_t;

typedef struct {
    loopback_end_t a;
    loopback_end_t b;
    bool           paired;
} loopback_pair_t;

static loopback_pair_t s_pairs[SAI_LOOPBACK_PAIRS];

static loopback_end_t *end_of(sai_device_t *d)
{
    for (uint32_t i = 0; i < SAI_LOOPBACK_PAIRS; i++) {
        if (&s_pairs[i].a.dev == d) return &s_pairs[i].a;
        if (&s_pairs[i].b.dev == d) return &s_pairs[i].b;
    }
    return NULL;
}

static int lb_write(sai_device_t *dev, const uint8_t *buf, uint32_t len)
{
    loopback_end_t *e = end_of(dev);
    if (e == NULL || buf == NULL) {
        return SAI_ERR_INVAL;
    }
    loopback_end_t *dst = (e->peer != NULL) ? e->peer : e;
    uint32_t queued = 0;
    for (uint32_t i = 0; i < len; i++) {
        if (!sai_ringbuf_put(&dst->ring, buf[i])) {
            break;                      /* drop when full (test device) */
        }
        queued++;
    }
    for (uint32_t i = 0; i < queued; i++) {
        (void)sai_isr_mbox_put(&dst->notify, 1u);
    }
    return (int)queued;
}

static int lb_read(sai_device_t *dev, uint8_t *buf, uint32_t len, int32_t timeout_ms)
{
    loopback_end_t *e = end_of(dev);
    if (e == NULL || buf == NULL) {
        return SAI_ERR_INVAL;
    }
    uint32_t got = 0;
    while (got < len) {
        uint8_t b;
        if (sai_ringbuf_get(&e->ring, &b)) {
            buf[got++] = b;
            continue;
        }
        if (got > 0) {
            break;                      /* partial read is valid */
        }
        uint8_t tok;
        if (sai_mbox_get(&e->notify, &tok, timeout_ms) != SAI_OK) {
            break;                      /* timed out with nothing */
        }
    }
    return (int)got;
}

static const sai_loopback_ops_t s_lb_ops = {
    .write = lb_write,
    .read  = lb_read,
};

sai_status_t sai_loopback_create(const char *name_a, const char *name_b,
                                 sai_device_t **out_a, sai_device_t **out_b)
{
    if (name_a == NULL || name_b == NULL || out_a == NULL || out_b == NULL) {
        return SAI_ERR_INVAL;
    }
    loopback_pair_t *p = NULL;
    for (uint32_t i = 0; i < SAI_LOOPBACK_PAIRS; i++) {
        if (!s_pairs[i].paired) {
            p = &s_pairs[i];
            break;
        }
    }
    if (p == NULL) {
        return SAI_ERR_FULL;
    }
    memset(p, 0, sizeof(*p));
    for (uint32_t i = 0; i < 2; i++) {
        loopback_end_t *e = (i == 0) ? &p->a : &p->b;
        strncpy(e->name, (i == 0) ? name_a : name_b,
                sizeof(e->name) - 1u);
        sai_ringbuf_init(&e->ring, e->storage, SAI_LOOPBACK_BUF);
        sai_status_t rc = sai_mbox_init(&e->notify, e->name, NULL,
                                        SAI_LOOPBACK_BUF);
        if (rc != SAI_OK) {
            return rc;
        }
        e->notify_init = true;
        rc = sai_device_register(&e->dev, e->name, SAI_DEVICE_TYPE_LOOPBK,
                                 0, NULL, 0,
                                 (const sai_dev_ops_t *)&s_lb_ops, NULL);
        if (rc != SAI_OK) {
            return rc;
        }
    }
    p->a.peer = &p->b;
    p->b.peer = &p->a;
    p->paired = true;
    *out_a = &p->a.dev;
    *out_b = &p->b.dev;
    return SAI_OK;
}
