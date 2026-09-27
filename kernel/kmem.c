/**
 * @file kernel/kmem.c
 * @brief Default kernel heap (static region) and the malloc-family shim.
 */
#include "internal.h"
#include <sai/mem.h>

#if CONFIG_SAI_HEAP_SIZE > 0
/* Alignment is a hard requirement for the heap allocator. */
static uint64_t    s_heap_region[CONFIG_SAI_HEAP_SIZE / 8u];
static sai_heap_t s_heap;
static bool     s_heap_ready;
#endif

sai_status_t sai_mem_init(void)
{
#if CONFIG_SAI_HEAP_SIZE > 0
    if (s_heap_ready) {
        return SAI_OK;
    }
    sai_status_t rc = sai_heap_init(&s_heap, "kheap", s_heap_region,
                                    (uint32_t)sizeof(s_heap_region));
    if (rc != SAI_OK) {
        return rc;
    }
    s_heap_ready = true;
    return SAI_OK;
#else
    return SAI_ERR_NOTSUP;   /* fully static build: no default heap */
#endif
}

void *sai_malloc(size_t size)
{
#if CONFIG_SAI_HEAP_SIZE > 0
    if (!s_heap_ready) {
        return NULL;
    }
    return sai_heap_alloc(&s_heap, (uint32_t)size);
#else
    (void)size;
    return NULL;
#endif
}

void *sai_calloc(size_t n, size_t size)
{
#if CONFIG_SAI_HEAP_SIZE > 0
    if (!s_heap_ready) {
        return NULL;
    }
    return sai_heap_calloc(&s_heap, (uint32_t)n, (uint32_t)size);
#else
    (void)n; (void)size;
    return NULL;
#endif
}

void *sai_realloc(void *ptr, size_t size)
{
#if CONFIG_SAI_HEAP_SIZE > 0
    if (!s_heap_ready) {
        return NULL;
    }
    return sai_heap_realloc(&s_heap, ptr, (uint32_t)size);
#else
    (void)ptr; (void)size;
    return NULL;
#endif
}

void sai_free(void *ptr)
{
#if CONFIG_SAI_HEAP_SIZE > 0
    if (s_heap_ready) {
        sai_heap_free(&s_heap, ptr);
    }
#else
    (void)ptr;
#endif
}

uint32_t sai_mem_free_bytes(void)
{
#if CONFIG_SAI_HEAP_SIZE > 0
    return s_heap_ready ? sai_heap_free_bytes(&s_heap) : 0u;
#else
    return 0u;
#endif
}

uint32_t sai_mem_fragmentation_pct(void)
{
#if CONFIG_SAI_HEAP_SIZE > 0
    return s_heap_ready ? sai_heap_fragmentation_pct(&s_heap) : 0u;
#else
    return 0u;
#endif
}

uint32_t sai_mem_max_free_block(void)
{
#if CONFIG_SAI_HEAP_SIZE > 0
    return s_heap_ready ? sai_heap_max_free_block(&s_heap) : 0u;
#else
    return 0u;
#endif
}
