/**
 * @file lib/net/net.h
 * @brief sai.L99 networking hook points (stub).
 *
 * No protocol implementation ships in this tree. This header defines the
 * integration contract a future TCP/IP stack (lwIP-style or original) would
 * implement: packet pool, RX worker attach point, and timer service.
 */
#ifndef SAI_NET_H
#define SAI_NET_H

#include <sai/types.h>
#include <sai/kernel.h>
#include <sai/ipc.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Packet descriptor: zero-copy buffers owned by a packet pool. */
typedef struct sai_net_pkt {
    struct sai_net_pkt *next;
    uint8_t  *data;
    uint32_t  len;
    uint32_t  cap;
} sai_net_pkt_t;

/** Packet pool over sai_mempool. */
typedef struct sai_net_pool {
    sai_mempool_t pool;
    uint32_t      pkt_len;
} sai_net_pool_t;

sai_status_t sai_net_pool_init(sai_net_pool_t *p, const char *name,
                               void *region, uint32_t num_pkts, uint32_t pkt_len);

/** Allocate/free packets (never blocks). */
sai_net_pkt_t *sai_net_pkt_alloc(sai_net_pool_t *p);
void sai_net_pkt_free(sai_net_pool_t *p, sai_net_pkt_t *pkt);

/**
 * RX delivery: a NIC driver calls this from its RX ISR with a filled packet.
 * The stub enqueues into the registered RX queue and wakes the stack worker
 * thread (ISR-safe). Returns SAI_ERR_NOENT when no stack is attached.
 */
sai_status_t sai_net_rx_deliver(sai_net_pool_t *p, sai_net_pkt_t *pkt);

/** Attach the (future) stack worker: sets the RX queue + wake thread. */
sai_status_t sai_net_stack_attach(sai_msgq_t *rxq, sai_thread_t *worker);

#ifdef __cplusplus
}
#endif

#endif /* SAI_NET_H */
