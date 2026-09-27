/**
 * @file lib/net/net.c
 * @brief sai.L99 networking stub: packet pool + RX hook points.
 */
#include "net.h"

static sai_msgq_t    *s_rxq;
static sai_thread_t  *s_worker;
static sai_net_pool_t *s_pool;

sai_status_t sai_net_pool_init(sai_net_pool_t *p, const char *name,
                               void *region, uint32_t num_pkts, uint32_t pkt_len)
{
    if (p == NULL || region == NULL) {
        return SAI_ERR_INVAL;
    }
    sai_status_t rc = sai_mempool_init(&p->pool, name, region, num_pkts, pkt_len);
    if (rc != SAI_OK) {
        return rc;
    }
    p->pkt_len = pkt_len;
    s_pool = p;
    return SAI_OK;
}

sai_net_pkt_t *sai_net_pkt_alloc(sai_net_pool_t *p)
{
    /* The descriptor itself lives in the packet buffer's head space. */
    sai_net_pkt_t *pkt = sai_mempool_alloc(&p->pool, SAI_NO_WAIT);
    if (pkt == NULL) {
        return NULL;
    }
    pkt->next = NULL;
    pkt->data = (uint8_t *)pkt + sizeof(sai_net_pkt_t);
    pkt->len  = 0u;
    pkt->cap  = p->pkt_len - sizeof(sai_net_pkt_t);
    return pkt;
}

void sai_net_pkt_free(sai_net_pool_t *p, sai_net_pkt_t *pkt)
{
    (void)sai_mempool_free(&p->pool, pkt);
}

sai_status_t sai_net_stack_attach(sai_msgq_t *rxq, sai_thread_t *worker)
{
    if (rxq == NULL || worker == NULL) {
        return SAI_ERR_INVAL;
    }
    s_rxq = rxq;
    s_worker = worker;
    return SAI_OK;
}

sai_status_t sai_net_rx_deliver(sai_net_pool_t *p, sai_net_pkt_t *pkt)
{
    (void)p;
    if (s_rxq == NULL || s_worker == NULL) {
        return SAI_ERR_NOENT;              /* no stack attached: drop */
    }
    return sai_isr_msgq_put(s_rxq, &pkt);  /* wake the worker */
}
