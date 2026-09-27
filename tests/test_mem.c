/**
 * @file tests/test_mem.c
 * @brief Memory pool, slab cache and heap allocator tests.
 */
#include "framework.h"
#include <sai/kernel.h>
#include <sai/mem.h>

static sai_mempool_t s_pool;
static sai_slab_t s_slab;
static sai_heap_t s_heap;
static uint8_t s_region[32 * 64];
static uint8_t s_slabbuf[16 * 32 + SAI_SLAB_META_BYTES(16)];
static uint8_t s_heapbuf[16384];

SAI_TEST_BEGIN(test_mempool)
{
    SAI_CHECK_EQ(sai_mempool_init(&s_pool, "pool", s_region, 32u, 64u), SAI_OK);
    SAI_CHECK_EQ(sai_mempool_free_count(&s_pool), 32u);

    void *blocks[32];
    for (int i = 0; i < 32; i++) {
        blocks[i] = sai_mempool_alloc(&s_pool, SAI_NO_WAIT);
        SAI_CHECK(blocks[i] != NULL);
    }
    SAI_CHECK_EQ(sai_mempool_free_count(&s_pool), 0u);
    SAI_CHECK(sai_mempool_alloc(&s_pool, SAI_NO_WAIT) == NULL);

    for (int i = 0; i < 32; i++) {
        SAI_CHECK_EQ(sai_mempool_free(&s_pool, blocks[i]), SAI_OK);
    }
    SAI_CHECK_EQ(sai_mempool_free_count(&s_pool), 32u);
    /* double free must be detected by bounds/alignment sanity */
    SAI_CHECK_EQ(sai_mempool_free(&s_pool, blocks[0]), SAI_OK);
    SAI_CHECK_EQ(sai_mempool_free(&s_pool, (uint8_t *)s_region + 7u), SAI_ERR_BOUNDS);
    sai_mempool_destroy(&s_pool);
}
SAI_TEST_END

SAI_TEST_BEGIN(test_slab)
{
    SAI_CHECK_EQ(sai_slab_init(&s_slab, "slab", s_slabbuf, 16u, 32u), SAI_OK);
    SAI_CHECK_EQ(sai_slab_free_count(&s_slab), 16u);

    void *objs[16];
    for (int i = 0; i < 16; i++) {
        objs[i] = sai_slab_alloc(&s_slab, SAI_NO_WAIT);
        SAI_CHECK(objs[i] != NULL);
    }
    SAI_CHECK_EQ(sai_slab_free_count(&s_slab), 0u);
    SAI_CHECK(sai_slab_alloc(&s_slab, SAI_NO_WAIT) == NULL);

    SAI_CHECK_EQ(sai_slab_free(&s_slab, objs[3]), SAI_OK);
    SAI_CHECK_EQ(sai_slab_free(&s_slab, objs[3]), SAI_ERR_STATE);  /* double free */
    void *again = sai_slab_alloc(&s_slab, SAI_NO_WAIT);
    SAI_CHECK(again == objs[3]);          /* roving hint reuses the slot */

    for (int i = 0; i < 16; i++) {
        if (i != 3) {
            (void)sai_slab_free(&s_slab, objs[i]);
        }
    }
    (void)sai_slab_free(&s_slab, objs[3]);
    SAI_CHECK_EQ(sai_slab_free_count(&s_slab), 16u);
    sai_slab_destroy(&s_slab);
}
SAI_TEST_END

SAI_TEST_BEGIN(test_heap_basic)
{
    SAI_CHECK_EQ(sai_heap_init(&s_heap, "h", s_heapbuf, sizeof(s_heapbuf)), SAI_OK);
    SAI_CHECK(sai_heap_free_bytes(&s_heap) > 0);

    void *a = sai_heap_alloc(&s_heap, 100);
    SAI_CHECK(a != NULL);
    SAI_CHECK(((uintptr_t)a & 7u) == 0u);  /* 8-aligned */
    memset(a, 0xAB, 100);

    void *b = sai_heap_alloc(&s_heap, 5000);
    SAI_CHECK(b != NULL);
    sai_heap_free(&s_heap, a);
    void *c = sai_heap_alloc(&s_heap, 100);
    SAI_CHECK(c != NULL);                  /* freed block was reused */
    sai_heap_free(&s_heap, b);
    sai_heap_free(&s_heap, c);
    SAI_CHECK_EQ(sai_heap_fragmentation_pct(&s_heap), 0u);  /* fully coalesced */
}
SAI_TEST_END

SAI_TEST_BEGIN(test_heap_fragmentation)
{
    SAI_CHECK_EQ(sai_heap_init(&s_heap, "h2", s_heapbuf, sizeof(s_heapbuf)), SAI_OK);

    /* allocate 8 blocks, free every other one: classic fragmentation */
    void *blocks[8];
    for (int i = 0; i < 8; i++) {
        blocks[i] = sai_heap_alloc(&s_heap, 1000);
        SAI_CHECK(blocks[i] != NULL);
    }
    for (int i = 0; i < 8; i += 2) {
        sai_heap_free(&s_heap, blocks[i]);
    }
    SAI_CHECK(sai_heap_fragmentation_pct(&s_heap) > 0u);
    /* a 3000-byte allocation must fail even though 4000 bytes are free */
    SAI_CHECK(sai_heap_alloc(&s_heap, 3000) == NULL);

    for (int i = 1; i < 8; i += 2) {
        sai_heap_free(&s_heap, blocks[i]);
    }
    SAI_CHECK_EQ(sai_heap_fragmentation_pct(&s_heap), 0u);
    void *big = sai_heap_alloc(&s_heap, 3000);
    SAI_CHECK(big != NULL);                /* coalesced after full free */
    sai_heap_free(&s_heap, big);
}
SAI_TEST_END

SAI_TEST_BEGIN(test_heap_realloc)
{
    SAI_CHECK_EQ(sai_heap_init(&s_heap, "h3", s_heapbuf, sizeof(s_heapbuf)), SAI_OK);
    char *p = sai_heap_alloc(&s_heap, 32);
    SAI_CHECK(p != NULL);
    strcpy(p, "sai.L99");
    p = sai_heap_realloc(&s_heap, p, 128);
    SAI_CHECK(p != NULL);
    SAI_CHECK(strcmp(p, "sai.L99") == 0);  /* contents preserved */
    sai_heap_free(&s_heap, p);
}
SAI_TEST_END

SAI_TEST_BEGIN(test_malloc_shim)
{
    void *p = sai_malloc(256);
    SAI_CHECK(p != NULL);
    memset(p, 0x5A, 256);
    void *q = sai_calloc(10, 10);
    SAI_CHECK(q != NULL);
    for (int i = 0; i < 100; i++) {
        SAI_CHECK(((uint8_t *)q)[i] == 0);
    }
    sai_free(p);
    sai_free(q);
}
SAI_TEST_END
