/**
 * @file kernel/mempool.c
 * @brief Fixed-block memory pool over a caller-provided region.
 *
 * Free blocks are chained intrusively through their first word. Deterministic
 * O(1) alloc/free; optional blocking with timeout when the pool is exhausted.
 */
#include "internal.h"
#include <sai/mem.h>

sai_status_t sai_mempool_init(sai_mempool_t *p, const char *name, void *region,
                              uint32_t num_blocks, uint32_t block_size)
{
    if (p == NULL || region == NULL || num_blocks == 0 || block_size < sizeof(void *)) {
        return SAI_ERR_INVAL;
    }
    memset(p, 0, sizeof(*p));
    p->kobj.type   = SAI_KOBJ_MEMPOOL;
    p->kobj.name   = name ? name : "pool";
    p->base        = region;
    p->block_size  = block_size;
    p->num_blocks  = num_blocks;
    p->free_blocks = num_blocks;

    /* chain all blocks */
    p->free_list = NULL;
    for (uint32_t i = num_blocks; i > 0; i--) {
        void *blk = (uint8_t *)region + (size_t)(i - 1u) * block_size;
        void **node = blk;
        *node = p->free_list;
        p->free_list = blk;
    }
    return SAI_OK;
}

void *sai_mempool_alloc(sai_mempool_t *p, int32_t timeout_ms)
{
    if (p == NULL) {
        return NULL;
    }
    for (;;) {
        uint32_t key = port_lock();
        if (p->free_list != NULL) {
            void *blk = p->free_list;
            p->free_list = *(void **)blk;
            p->free_blocks--;
            port_unlock(key);
            return blk;
        }
        port_unlock(key);

        if (timeout_ms == SAI_NO_WAIT || port_in_isr() || _sai_current == NULL) {
            return NULL;
        }
        if (_sai_wq_block(&p->wq_store, 0u, timeout_ms) != SAI_OK) {
            return NULL;
        }
    }
}

sai_status_t sai_mempool_free(sai_mempool_t *p, void *block)
{
    if (p == NULL || block == NULL) {
        return SAI_ERR_INVAL;
    }
    /* sanity: block must be inside the region and block-aligned */
    uintptr_t off = (uintptr_t)block - (uintptr_t)p->base;
    if (off >= (uintptr_t)p->num_blocks * p->block_size ||
        (off % p->block_size) != 0u) {
        return SAI_ERR_BOUNDS;
    }

    uint32_t key = port_lock();
    void **node = block;
    *node = p->free_list;
    p->free_list = block;
    p->free_blocks++;
    port_unlock(key);

    sai_thread_t *w = p->wq_store.head;
    if (w != NULL) {
        (void)_sai_wq_remove(w);
        if (w->wq_flags & SAI_WQF_TIMED) {
            _sai_deadline_remove(w);
            w->wq_flags &= (uint8_t)~SAI_WQF_TIMED;
        }
        w->wq_result = (uint32_t)SAI_OK;
        w->state = SAI_THREAD_READY;
        _sai_ready_insert(w);
        if (w->prio < (_sai_current ? _sai_current->prio : 255u)) {
            _sai_schedule();
        }
    }
    return SAI_OK;
}

uint32_t sai_mempool_free_count(const sai_mempool_t *p)
{
    return p ? p->free_blocks : 0u;
}

sai_status_t sai_mempool_destroy(sai_mempool_t *p)
{
    if (p == NULL) {
        return SAI_ERR_INVAL;
    }
    if (_sai_wq_count(&p->wq_store) != 0u) {
        return SAI_ERR_BUSY;
    }
    p->kobj.type = SAI_KOBJ_NONE;
    return SAI_OK;
}
