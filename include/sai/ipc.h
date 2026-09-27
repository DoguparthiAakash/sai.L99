/**
 * @file sai/ipc.h
 * @brief Inter-thread communication: message queues, mailboxes, pipes, event flags.
 */
#ifndef SAI_IPC_H
#define SAI_IPC_H

#include <sai/types.h>
#include <sai/sync.h>
#include <sai/config.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Message queue (fixed-size messages)                                 */
/* ------------------------------------------------------------------ */
typedef struct sai_msgq {
    sai_kobj_t  kobj;
    uint8_t    *buf;          /**< Storage (owned if dynamic).            */
    uint32_t    msg_size;
    uint32_t    max_msgs;
    uint32_t    head;          /**< Read index.                            */
    uint32_t    count;
    bool        alloc;         /**< Storage was heap-allocated.            */
    struct sai_waitqueue *put_wait;  /**< Full-queue waiters.           */
    struct sai_waitqueue *get_wait;  /**< Empty-queue waiters.          */
    struct sai_waitqueue  put_store; /**< Embedded storage.             */
    struct sai_waitqueue  get_store; /**< Embedded storage.             */
} sai_msgq_t;

/**
 * Initialize a message queue. @p buffer must hold @p max_msgs messages of
 * @p msg_size bytes (caller-provided for static use), or pass NULL to
 * allocate from the kernel heap.
 */
sai_status_t sai_msgq_init(sai_msgq_t *q, const char *name, void *buffer,
                           uint32_t msg_size, uint32_t max_msgs);

sai_status_t sai_msgq_put(sai_msgq_t *q, const void *msg, int32_t timeout_ms);
sai_status_t sai_msgq_get(sai_msgq_t *q, void *msg, int32_t timeout_ms);

/** Non-blocking put; overwrites the oldest message if full. */
sai_status_t sai_msgq_put_overwrite(sai_msgq_t *q, const void *msg);

/** ISR-safe put: never blocks; SAI_ERR_FULL when the queue is full. */
sai_status_t sai_isr_msgq_put(struct sai_msgq *q, const void *msg);

uint32_t sai_msgq_count(const sai_msgq_t *q);
uint32_t sai_msgq_space(const sai_msgq_t *q);
sai_status_t sai_msgq_destroy(sai_msgq_t *q);

/* ------------------------------------------------------------------ */
/* Mailbox (byte-oriented FIFO)                                        */
/* ------------------------------------------------------------------ */
typedef struct sai_mbox {
    sai_kobj_t  kobj;
    uint8_t    *buf;
    uint32_t    size;
    uint32_t    head;
    uint32_t    count;
    bool        alloc;
    struct sai_waitqueue *put_wait;
    struct sai_waitqueue *get_wait;
    struct sai_waitqueue  put_store; /**< Embedded storage.             */
    struct sai_waitqueue  get_store; /**< Embedded storage.             */
} sai_mbox_t;

sai_status_t sai_mbox_init(sai_mbox_t *b, const char *name, void *buffer, uint32_t size);
sai_status_t sai_mbox_put(sai_mbox_t *b, uint8_t byte, int32_t timeout_ms);
sai_status_t sai_mbox_get(sai_mbox_t *b, uint8_t *byte, int32_t timeout_ms);
/** ISR-safe put; SAI_ERR_FULL when the mailbox is full. */
sai_status_t sai_isr_mbox_put(struct sai_mbox *b, uint8_t byte);
uint32_t sai_mbox_count(const sai_mbox_t *b);
sai_status_t sai_mbox_destroy(sai_mbox_t *b);

/* ------------------------------------------------------------------ */
/* Pipe (byte stream with scatter read/write)                          */
/* ------------------------------------------------------------------ */
typedef struct sai_pipe {
    sai_kobj_t  kobj;
    uint8_t    *buf;
    uint32_t    size;
    uint32_t    head;
    uint32_t    count;
    bool        alloc;
    struct sai_waitqueue *put_wait;
    struct sai_waitqueue *get_wait;
    struct sai_waitqueue  put_store; /**< Embedded storage.             */
    struct sai_waitqueue  get_store; /**< Embedded storage.             */
} sai_pipe_t;

sai_status_t sai_pipe_init(sai_pipe_t *p, const char *name, void *buffer, uint32_t size);
/** Write up to @p len bytes; returns bytes written (may be partial). */
int32_t sai_pipe_write(sai_pipe_t *p, const void *data, uint32_t len, int32_t timeout_ms);
/** Read up to @p len bytes; returns bytes read (may be partial). */
int32_t sai_pipe_read(sai_pipe_t *p, void *data, uint32_t len, int32_t timeout_ms);
uint32_t sai_pipe_count(const sai_pipe_t *p);
sai_status_t sai_pipe_destroy(sai_pipe_t *p);

/* ------------------------------------------------------------------ */
/* Event flags                                                         */
/* ------------------------------------------------------------------ */
/** Wait for ALL of the requested flags (default). */
#define SAI_EVENT_WAIT_ALL    0x1u
/** Wait for ANY of the requested flags. */
#define SAI_EVENT_WAIT_ANY    0x4u
/** Clear the flags that satisfied the wait before returning. */
#define SAI_EVENT_CONSUME     0x2u

typedef struct sai_event {
    sai_kobj_t  kobj;
    uint32_t    flags;
    struct sai_waitqueue *waiters;
    struct sai_waitqueue  wq_store;  /**< Embedded storage.             */
} sai_event_t;

sai_status_t sai_event_init(sai_event_t *e, const char *name);
sai_status_t sai_event_set(sai_event_t *e, uint32_t flags);
/** ISR-safe set: never blocks. */
sai_status_t sai_isr_event_set(struct sai_event *e, uint32_t flags);
/** Wait for events. @p timeout_ms < 0 = forever. */
sai_status_t sai_event_wait(sai_event_t *e, uint32_t flags, uint32_t opts,
                            uint32_t *set_flags, int32_t timeout_ms);
uint32_t sai_event_get(const sai_event_t *e);
sai_status_t sai_event_clear(sai_event_t *e, uint32_t flags);
sai_status_t sai_event_destroy(sai_event_t *e);

#ifdef __cplusplus
}
#endif

#endif /* SAI_IPC_H */
