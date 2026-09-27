/**
 * @file kernel/slab.c
 * @brief Slab cache: fixed-size object allocator with off-object used bitmap.
 *
 * The caller owns the object storage (base buffer) and the bitmap storage is
 * appended by sai_slab_init() into a region the caller provides via
 * SAI_SLAB_META_BYTES(); this keeps object layout fully under caller control
 * (unlike mempools, which own their storage).
 */
#include "internal.h"
#include <sai/mem.h>

sai_status_t sai_slab_init(sai_slab_t *s, const char *name, void *buffer,
                           uint32_t num_objs, uint32_t obj_size)
{
    if (s == NULL || buffer == NULL || num_objs == 0 || obj_size < sizeof(uint32_t)) {
        return SAI_ERR_INVAL;
    }
    memset(s, 0, sizeof(*s));
    s->kobj.type  = SAI_KOBJ_SLAB;
    s->kobj.name  = name ? name : "slab";
    s->base       = buffer;
    s->obj_size   = obj_size;
    s->stride     = (obj_size + sizeof(uint32_t) - 1u) & ~(uint32_t)(sizeof(uint32_t) - 1u);
    s->num_objs   = num_objs;
    s->free_objs  = num_objs;
    s->next_hint  = 0;

    /* bitmap lives immediately after the last object in the caller's buffer;
     * callers size their buffer as num_objs*stride + SAI_SLAB_META_BYTES(n) */
    uint8_t *bitmap = (uint8_t *)buffer + (size_t)num_objs * s->stride;
    memset(bitmap, 0, SAI_SLAB_META_BYTES(num_objs));
    s->used_map = bitmap;
    return SAI_OK;
}

SAI_ALWAYS_INLINE bool map_get(const sai_slab_t *s, uint32_t i)
{
    return (s->used_map[i >> 3] & (uint8_t)(1u << (i & 7u))) != 0u;
}

SAI_ALWAYS_INLINE void map_set(sai_slab_t *s, uint32_t i, int v)
{
    uint8_t mask = (uint8_t)(1u << (i & 7u));
    if (v) {
        s->used_map[i >> 3] |= mask;
    } else {
        s->used_map[i >> 3] &= (uint8_t)~mask;
    }
}

static void *slab_obj(sai_slab_t *s, uint32_t i)
{
    return s->base + (size_t)i * s->stride;
}

void *sai_slab_alloc(sai_slab_t *s, int32_t timeout_ms)
{
    if (s == NULL) {
        return NULL;
    }
    for (;;) {
        uint32_t key = port_lock();
        if (s->free_objs != 0u) {
            /* roving scan from next_hint */
            for (uint32_t k = 0; k < s->num_objs; k++) {
                uint32_t i = (s->next_hint + k) % s->num_objs;
                if (!map_get(s, i)) {
                    map_set(s, i, true);
                    s->free_objs--;
                    s->next_hint = (i + 1u) % s->num_objs;
                    port_unlock(key);
                    return slab_obj(s, i);
                }
            }
        }
        port_unlock(key);

        if (timeout_ms == SAI_NO_WAIT || port_in_isr() || _sai_current == NULL) {
            return NULL;
        }
        if (_sai_wq_block(&s->wq_store, 0u, timeout_ms) != SAI_OK) {
            return NULL;
        }
    }
}

sai_status_t sai_slab_free(sai_slab_t *s, void *obj)
{
    if (s == NULL || obj == NULL) {
        return SAI_ERR_INVAL;
    }
    uintptr_t off = (uintptr_t)obj - (uintptr_t)s->base;
    if (off >= (uintptr_t)s->num_objs * s->stride || (off % s->stride) != 0u) {
        return SAI_ERR_BOUNDS;
    }
    uint32_t i = (uint32_t)(off / s->stride);
    if (!map_get(s, i)) {
        return SAI_ERR_STATE;            /* double free */
    }

    uint32_t key = port_lock();
    map_set(s, i, false);
    s->free_objs++;
    if (i < s->next_hint) {
        s->next_hint = i;                /* bias toward just-freed slots */
    }
    port_unlock(key);

    sai_thread_t *w = s->wq_store.head;
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

uint32_t sai_slab_free_count(const sai_slab_t *s)
{
    return s ? s->free_objs : 0u;
}

sai_status_t sai_slab_destroy(sai_slab_t *s)
{
    if (s == NULL) {
        return SAI_ERR_INVAL;
    }
    if (_sai_wq_count(&s->wq_store) != 0u) {
        return SAI_ERR_BUSY;
    }
    s->kobj.type = SAI_KOBJ_NONE;
    return SAI_OK;
}
