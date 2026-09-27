/**
 * @file sai/ringbuf.h
 * @brief Lock-free single-producer/single-consumer ring buffer.
 *
 * Used by serial drivers between ISR (producer) and thread (consumer) and
 * by the console backends. SPSC only: each side must have exactly one actor.
 */
#ifndef SAI_RINGBUF_H
#define SAI_RINGBUF_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sai_ringbuf {
    uint8_t *buf;
    uint32_t size;      /**< Power of two.                    */
    volatile uint32_t head;   /**< Writer index (producer owns). */
    volatile uint32_t tail;   /**< Reader index (consumer owns). */
} sai_ringbuf_t;

/** Initialize a ring over caller-provided storage (size must be a power of 2). */
static inline void sai_ringbuf_init(sai_ringbuf_t *r, void *storage, uint32_t size)
{
    r->buf  = (uint8_t *)storage;
    r->size = size;
    r->head = 0u;
    r->tail = 0u;
}

/** Bytes currently buffered. */
static inline uint32_t sai_ringbuf_count(const sai_ringbuf_t *r)
{
    return (uint32_t)(r->head - r->tail);
}

/** Free space in bytes. */
static inline uint32_t sai_ringbuf_space(const sai_ringbuf_t *r)
{
    return r->size - sai_ringbuf_count(r);
}

static inline bool sai_ringbuf_empty(const sai_ringbuf_t *r)
{
    return r->head == r->tail;
}

static inline bool sai_ringbuf_full(const sai_ringbuf_t *r)
{
    return sai_ringbuf_count(r) == r->size;
}

/** Push one byte (producer side). Returns false when full. */
static inline bool sai_ringbuf_put(sai_ringbuf_t *r, uint8_t b)
{
    uint32_t head = r->head;
    if ((uint32_t)(head - r->tail) == r->size) {
        return false;
    }
    r->buf[head & (r->size - 1u)] = b;
    r->head = head + 1u;
    return true;
}

/** Pop one byte (consumer side). Returns false when empty. */
static inline bool sai_ringbuf_get(sai_ringbuf_t *r, uint8_t *out)
{
    uint32_t tail = r->tail;
    if (r->head == tail) {
        return false;
    }
    *out = r->buf[tail & (r->size - 1u)];
    r->tail = tail + 1u;
    return true;
}

/** Peek at the oldest byte without consuming it. */
static inline bool sai_ringbuf_peek(const sai_ringbuf_t *r, uint8_t *out)
{
    if (r->head == r->tail) {
        return false;
    }
    *out = r->buf[r->tail & (r->size - 1u)];
    return true;
}

#ifdef __cplusplus
}
#endif

#endif /* SAI_RINGBUF_H */
