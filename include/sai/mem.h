/**
 * @file sai/mem.h
 * @brief Memory management: pools, slab caches, TLSF-style heap, malloc shim.
 */
#ifndef SAI_MEM_H
#define SAI_MEM_H

#include <sai/types.h>
#include <sai/sync.h>
#include <sai/config.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Heap index geometry: first-level classes and second-level subdivisions. */
/* First-level index must cover the full 32-bit size range (fl = floor(log2
 * of block size), so 32 entries index every possible block); the second
 * level subdivides each class into 8 bins. */
#define SAI_HEAP_FL_COUNT 32u
#define SAI_HEAP_SL_COUNT 8u

/* ------------------------------------------------------------------ */
/* Memory pool: fixed-size blocks from a caller-provided region        */
/* ------------------------------------------------------------------ */
typedef struct sai_mempool {
    sai_kobj_t  kobj;
    uint8_t    *base;          /**< Region base.                           */
    uint32_t    block_size;    /**< Usable size per block (aligned).       */
    uint32_t    num_blocks;
    uint32_t    free_blocks;
    void       *free_list;     /**< Intrusive free list.                   */
    struct sai_waitqueue  wq_store;  /**< Embedded waitqueue storage.    */
} sai_mempool_t;

/**
 * Initialize a pool over @p region with @p num_blocks blocks of @p block_size
 * bytes. @p region must be aligned to SAI_STACK_ALIGN and hold at least
 * num_blocks * block_size bytes.
 */
sai_status_t sai_mempool_init(sai_mempool_t *p, const char *name, void *region,
                              uint32_t num_blocks, uint32_t block_size);

/** Allocate one block (never blocks on init'd pools with free blocks). */
void *sai_mempool_alloc(sai_mempool_t *p, int32_t timeout_ms);
/** Free a block previously returned by sai_mempool_alloc(). */
sai_status_t sai_mempool_free(sai_mempool_t *p, void *block);
uint32_t sai_mempool_free_count(const sai_mempool_t *p);
sai_status_t sai_mempool_destroy(sai_mempool_t *p);

/* ------------------------------------------------------------------ */
/* Slab allocator: fixed-size object caches                            */
/* ------------------------------------------------------------------ */
/**
 * Slab cache over a caller-provided buffer. Objects are laid out back to
 * back; a per-object header is stored off-object so the caller controls the
 * object layout exactly (unlike pools, which own the storage).
 */
typedef struct sai_slab {
    sai_kobj_t   kobj;
    uint8_t     *base;
    uint32_t     obj_size;    /**< Requested object size.                  */
    uint32_t     stride;      /**< Allocated stride per object.            */
    uint32_t     num_objs;
    uint32_t     free_objs;
    uint8_t     *used_map;    /**< 1 bit per object.                       */
    uint32_t     next_hint;   /**< Allocation roving pointer.              */
    struct sai_waitqueue  wq_store;  /**< Embedded waitqueue storage.    */
} sai_slab_t;

/** Bytes of metadata needed for a slab of @p n objects. */
#define SAI_SLAB_META_BYTES(n) (((n) + 7u) / 8u)

sai_status_t sai_slab_init(sai_slab_t *s, const char *name, void *buffer,
                           uint32_t num_objs, uint32_t obj_size);
/** Allocate one object, or NULL. */
void *sai_slab_alloc(sai_slab_t *s, int32_t timeout_ms);
sai_status_t sai_slab_free(sai_slab_t *s, void *obj);
uint32_t sai_slab_free_count(const sai_slab_t *s);
sai_status_t sai_slab_destroy(sai_slab_t *s);

/* ------------------------------------------------------------------ */
/* Heap allocator (TLSF-style, two-level bitmap)                       */
/* ------------------------------------------------------------------ */
typedef struct sai_heap {
    sai_kobj_t  kobj;
    uint8_t    *base;
    uint32_t    size;
    uint8_t     initialized;
    /* TLSF-style structure; opaque here, defined in kernel/mem/heap.c */
    uint32_t    fl_bitmap;
    uint16_t    sl_bitmap[SAI_HEAP_FL_COUNT];
    /* Free lists: singly-linked through block headers. */
    void       *sl_head[SAI_HEAP_FL_COUNT][SAI_HEAP_SL_COUNT];
} sai_heap_t;

/** Initialize a heap over a caller-provided region (must be 8-aligned). */
sai_status_t sai_heap_init(sai_heap_t *h, const char *name, void *region, uint32_t size);
/** Allocate @p size bytes (8-aligned), or NULL. */
void *sai_heap_alloc(sai_heap_t *h, uint32_t size);
/** Allocate zeroed memory. */
void *sai_heap_calloc(sai_heap_t *h, uint32_t n, uint32_t size);
/** Reallocate (may move). */
void *sai_heap_realloc(sai_heap_t *h, void *ptr, uint32_t size);
/** Free a pointer obtained from this heap (NULL is a no-op). */
void sai_heap_free(sai_heap_t *h, void *ptr);
/** Largest allocation currently guaranteed to succeed. */
uint32_t sai_heap_max_free_block(const sai_heap_t *h);
/** Total free bytes (diagnostics). */
uint32_t sai_heap_free_bytes(const sai_heap_t *h);
/** Fragmentation metric 0..100 (100 = worst). */
uint32_t sai_heap_fragmentation_pct(const sai_heap_t *h);

/* ------------------------------------------------------------------ */
/* malloc-family shim over the kernel heap                             */
/* ------------------------------------------------------------------ */
/** Bootstrap the default heap from a static region (called at boot). */
sai_status_t sai_mem_init(void);

/** Default-heap allocation (never blocks). */
void *sai_malloc(size_t size);
void *sai_calloc(size_t n, size_t size);
void *sai_realloc(void *ptr, size_t size);
void sai_free(void *ptr);

/** Default-heap diagnostics. */
uint32_t sai_mem_free_bytes(void);
uint32_t sai_mem_fragmentation_pct(void);
uint32_t sai_mem_max_free_block(void);

#ifdef __cplusplus
}
#endif

#endif /* SAI_MEM_H */
