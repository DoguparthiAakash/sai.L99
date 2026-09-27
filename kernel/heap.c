/**
 * @file kernel/heap.c
 * @brief TLSF-style O(1) heap allocator with two-level segregated fit.
 *
 * Block layout (8-byte aligned):
 *   [ header: size | bit0=used | bit1=prev_used ][ payload ... ]
 * Free blocks carry the payload-sized footer with the same size for
 * coalescing with the previous block. Split/merge are O(1); the two-level
 * bitmap (first-level = power-of-two class, second-level = 8 subdivisions)
 * keeps allocation bounded and fragmentation behavior predictable.
 */
#include "internal.h"
#include <sai/mem.h>

#define FL_INDEX_MAX   SAI_HEAP_FL_COUNT
#define SL_INDEX_COUNT SAI_HEAP_SL_COUNT
#define MIN_BLOCK_SIZE 16u
#define ALIGNMENT      8u
#define HDR_SIZE       4u    /* header word (size + flags) */
#define FTR_SIZE       4u    /* footer word for free blocks */

typedef struct {
    uint32_t header;             /* block size incl. header/footer; bit0 used, bit1 prev_used */
} block_hdr_t;

/* Header accessors: bits 0=USED, 1=PREV_USED. */
SAI_ALWAYS_INLINE uint32_t hdr_size(const block_hdr_t *h) { return h->header & ~3u; }
SAI_ALWAYS_INLINE bool hdr_used(const block_hdr_t *h) { return (h->header & 1u) != 0u; }
SAI_ALWAYS_INLINE bool hdr_prev_used(const block_hdr_t *h) { return (h->header & 2u) != 0u; }
SAI_ALWAYS_INLINE void hdr_set_used(block_hdr_t *h, bool u) { if (u) h->header |= 1u; else h->header &= ~1u; }
SAI_ALWAYS_INLINE void hdr_set_prev_used(block_hdr_t *h, bool u) { if (u) h->header |= 2u; else h->header &= ~2u; }

/* Free-list node stored in the payload of free blocks. */
typedef struct free_node {
    struct free_node *prev;      /* within its free list */
    struct free_node *next;
} free_node_t;

SAI_ALWAYS_INLINE free_node_t *blk_free(block_hdr_t *h)
{
    return (free_node_t *)((uint8_t *)h + HDR_SIZE);
}

SAI_ALWAYS_INLINE block_hdr_t *next_hdr(block_hdr_t *h)
{
    return (block_hdr_t *)((uint8_t *)h + hdr_size(h));
}

SAI_ALWAYS_INLINE block_hdr_t *prev_hdr_from_footer(block_hdr_t *h)
{
    uint32_t *footer = (uint32_t *)h - 1;      /* footer sits right before this header */
    return (block_hdr_t *)((uint8_t *)h - (*footer & ~3u));
}

/* ------------------------------------------------------------------ */
/* Mapping size <-> indices                                            */
/* ------------------------------------------------------------------ */
static void mapping_insert(uint32_t size, uint32_t *fl, uint32_t *sl)
{
    /* FL: floor(log2(size)); SL: 8 subdivisions within the class. */
    uint32_t flv = 0;
    while ((size >> flv) != 0u) { flv++; }
    if (flv > 0) { flv--; }
    *fl = flv;
    uint32_t shift = (flv >= 3u) ? (flv - 3u) : 0u;
    *sl = (size >> shift) & (SL_INDEX_COUNT - 1u);
}

static void mapping_search(uint32_t size, uint32_t *fl, uint32_t *sl)
{
    /* Round the request up to the SL granularity of its class so that ANY
     * block found in the target bin is guaranteed to fit. */
    uint32_t flv = 0;
    while ((size >> flv) != 0u) { flv++; }
    if (flv > 0) { flv--; }
    uint32_t shift = (flv >= 3u) ? (flv - 3u) : 0u;
    size = (size + ((1u << shift) - 1u)) & ~((1u << shift) - 1u);
    mapping_insert(size, fl, sl);
}

static uint32_t align_size(uint32_t size)
{
    if (size < MIN_BLOCK_SIZE) {
        size = MIN_BLOCK_SIZE;
    }
    return (size + ALIGNMENT - 1u) & ~(ALIGNMENT - 1u);
}

/* ------------------------------------------------------------------ */
/* Free list management                                                */
/* ------------------------------------------------------------------ */
static void heap_insert_block(sai_heap_t *h, block_hdr_t *blk)
{
    uint32_t fl, sl;
    mapping_insert(hdr_size(blk), &fl, &sl);
    if (fl >= FL_INDEX_MAX) {
        return;                            /* too large: shouldn't happen */
    }
    free_node_t *n = blk_free(blk);
    n->prev = NULL;
    n->next = h->sl_head[fl][sl];
    if (n->next != NULL) {
        n->next->prev = n;
    }
    h->sl_head[fl][sl] = n;
    h->sl_bitmap[fl] |= (uint16_t)(1u << sl);
    h->fl_bitmap |= (1u << fl);
}

static void heap_remove_block(sai_heap_t *h, block_hdr_t *blk)
{
    uint32_t fl, sl;
    mapping_insert(hdr_size(blk), &fl, &sl);
    free_node_t *n = blk_free(blk);
    if (n->prev != NULL) {
        n->prev->next = n->next;
    } else {
        h->sl_head[fl][sl] = n->next;
    }
    if (n->next != NULL) {
        n->next->prev = n->prev;
    }
    if (h->sl_head[fl][sl] == NULL) {
        h->sl_bitmap[fl] &= (uint16_t)~(1u << sl);
        if (h->sl_bitmap[fl] == 0u) {
            h->fl_bitmap &= ~(1u << fl);
        }
    }
    n->prev = n->next = NULL;
}

static block_hdr_t *heap_search_suitable(sai_heap_t *h, uint32_t size)
{
    uint32_t fl, sl;
    mapping_search(size, &fl, &sl);

    /* first candidate: same FL, next SL */
    uint16_t sl_map = h->sl_bitmap[fl] & (uint16_t)(~0u << sl);
    if (sl_map == 0u) {
        /* next FL with any free block (guard fl == 31: no larger class) */
        uint32_t fl_map = (fl + 1u >= FL_INDEX_MAX)
                              ? 0u
                              : (h->fl_bitmap & (~0u << (fl + 1u)));
        if (fl_map == 0u) {
            return NULL;
        }
        uint32_t fl2 = 0;
        while ((fl_map & 1u) == 0u) { fl_map >>= 1u; fl2++; }
        fl = fl2;
        sl_map = h->sl_bitmap[fl];
    }
    uint32_t sl2 = 0;
    while ((sl_map & 1u) == 0u) { sl_map >>= 1u; sl2++; }

    /* sl_head stores the free-list node (which lives in the payload at
     * block + HDR_SIZE); convert it back to the block header. */
    free_node_t *node = (free_node_t *)h->sl_head[fl][sl2];
    return (block_hdr_t *)(void *)((uint8_t *)node - HDR_SIZE);
}

/* ------------------------------------------------------------------ */
/* Split / merge                                                       */
/* ------------------------------------------------------------------ */
SAI_ALWAYS_INLINE void hdr_set_size(block_hdr_t *h, uint32_t size)
{
    h->header = (h->header & 3u) | (size & ~3u);
}

static void split_block(sai_heap_t *h, block_hdr_t *blk, uint32_t size)
{
    if (hdr_size(blk) >= size + MIN_BLOCK_SIZE) {
        block_hdr_t *rest = (block_hdr_t *)((uint8_t *)blk + size);
        rest->header = (hdr_size(blk) - size) & ~3u;
        rest->header |= hdr_prev_used(blk) ? 2u : 0u;
        hdr_set_prev_used(rest, true);     /* predecessor = used blk */
        hdr_set_used(rest, false);

        uint32_t *footer = (uint32_t *)((uint8_t *)rest + hdr_size(rest) - FTR_SIZE);
        *footer = rest->header;

        hdr_set_prev_used(next_hdr(rest), false);
        hdr_set_size(blk, size);

        heap_insert_block(h, rest);
    }
}

static block_hdr_t *merge_prev(sai_heap_t *h, block_hdr_t *blk)
{
    if (!hdr_prev_used(blk)) {
        block_hdr_t *prev = prev_hdr_from_footer(blk);
        heap_remove_block(h, prev);
        hdr_set_size(prev, hdr_size(prev) + hdr_size(blk));
        uint32_t *footer = (uint32_t *)((uint8_t *)prev + hdr_size(prev) - FTR_SIZE);
        *footer = prev->header;
        hdr_set_prev_used(next_hdr(prev), false);
        blk = prev;
    }
    return blk;
}

static block_hdr_t *merge_next(sai_heap_t *h, block_hdr_t *blk)
{
    block_hdr_t *next = next_hdr(blk);
    if ((uint8_t *)next < h->base + h->size && !hdr_used(next)) {
        heap_remove_block(h, next);
        hdr_set_size(blk, hdr_size(blk) + hdr_size(next));
        uint32_t *footer = (uint32_t *)((uint8_t *)blk + hdr_size(blk) - FTR_SIZE);
        *footer = blk->header;
        hdr_set_prev_used(next_hdr(blk), false);
    }
    return blk;
}

/* ------------------------------------------------------------------ */
/* Heap lifecycle + API                                                */
/* ------------------------------------------------------------------ */
sai_status_t sai_heap_init(sai_heap_t *h, const char *name, void *region, uint32_t size)
{
    if (h == NULL || region == NULL || size < 256u) {
        return SAI_ERR_INVAL;
    }
    if (((uintptr_t)region & (ALIGNMENT - 1u)) != 0u) {
        return SAI_ERR_INVAL;              /* region must be 8-aligned */
    }
    memset(h, 0, sizeof(*h));
    h->kobj.type = SAI_KOBJ_HEAP;
    h->kobj.name = name ? name : "heap";
    h->base = region;
    h->size = size & ~(ALIGNMENT - 1u);
    h->initialized = 1u;

    /* Reserve one block at the very end as a permanently "used" boundary
     * marker: every real block's next_hdr() then lands inside the region,
     * so split/alloc never write past the end. */
    uint32_t usable = h->size - ALIGNMENT;

    /* one big free block covering the usable region */
    block_hdr_t *blk = (block_hdr_t *)h->base;
    blk->header = usable & ~3u;
    hdr_set_used(blk, false);
    hdr_set_prev_used(blk, true);
    uint32_t *footer = (uint32_t *)((uint8_t *)blk + usable - FTR_SIZE);
    *footer = blk->header;
    heap_insert_block(h, blk);

    block_hdr_t *guard = (block_hdr_t *)((uint8_t *)h->base + usable);
    guard->header = ALIGNMENT | 1u;      /* used, prev_used=false */
    return SAI_OK;
}

void *sai_heap_alloc(sai_heap_t *h, uint32_t size)
{
    if (h == NULL || !h->initialized) {
        return NULL;
    }
    uint32_t req = align_size(size + HDR_SIZE);
    uint32_t key = port_lock();

    block_hdr_t *blk = heap_search_suitable(h, req);
    if (blk == NULL) {
        port_unlock(key);
        return NULL;
    }
    heap_remove_block(h, blk);
    hdr_set_used(blk, true);
    split_block(h, blk, req);
    hdr_set_prev_used(next_hdr(blk), true);

    port_unlock(key);
    return (uint8_t *)blk + HDR_SIZE;
}

void sai_heap_free(sai_heap_t *h, void *ptr)
{
    if (h == NULL || ptr == NULL || !h->initialized) {
        return;
    }
    block_hdr_t *blk = (block_hdr_t *)((uint8_t *)ptr - HDR_SIZE);
    uint32_t key = port_lock();

    hdr_set_used(blk, false);
    uint32_t *footer = (uint32_t *)((uint8_t *)blk + hdr_size(blk) - FTR_SIZE);
    *footer = blk->header;
    hdr_set_prev_used(next_hdr(blk), false);

    blk = merge_prev(h, blk);
    blk = merge_next(h, blk);
    heap_insert_block(h, blk);

    port_unlock(key);
}

void *sai_heap_calloc(sai_heap_t *h, uint32_t n, uint32_t size)
{
    uint64_t total = (uint64_t)n * (uint64_t)size;
    if (total > 0xFFFFFFFFull) {
        return NULL;
    }
    void *p = sai_heap_alloc(h, (uint32_t)total);
    if (p != NULL) {
        memset(p, 0, (size_t)total);
    }
    return p;
}

void *sai_heap_realloc(sai_heap_t *h, void *ptr, uint32_t size)
{
    if (ptr == NULL) {
        return sai_heap_alloc(h, size);
    }
    if (size == 0) {
        sai_heap_free(h, ptr);
        return NULL;
    }
    block_hdr_t *blk = (block_hdr_t *)((uint8_t *)ptr - HDR_SIZE);
    uint32_t cur = hdr_size(blk) - HDR_SIZE;

    if (align_size(size + HDR_SIZE) <= cur) {
        return ptr;                        /* still fits */
    }
    void *np = sai_heap_alloc(h, size);
    if (np == NULL) {
        return NULL;
    }
    memcpy(np, ptr, cur < size ? cur : size);
    sai_heap_free(h, ptr);
    return np;
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */
uint32_t sai_heap_free_bytes(const sai_heap_t *h)
{
    if (h == NULL || !h->initialized) {
        return 0;
    }
    uint32_t key = port_lock();
    uint32_t total = 0;
    for (uint32_t fl = 0; fl < FL_INDEX_MAX; fl++) {
        uint16_t sl_map = h->sl_bitmap[fl];
        while (sl_map) {
            uint32_t sl = 0;
            while ((sl_map & 1u) == 0u) { sl_map >>= 1u; sl++; }
            for (free_node_t *n = h->sl_head[fl][sl]; n != NULL; n = n->next) {
                block_hdr_t *b = (block_hdr_t *)((uint8_t *)n - HDR_SIZE);
                total += hdr_size(b);
            }
            sl_map &= (uint16_t)(sl_map - 1u);
        }
    }
    port_unlock(key);
    return total;
}

uint32_t sai_heap_max_free_block(const sai_heap_t *h)
{
    if (h == NULL || !h->initialized) {
        return 0;
    }
    uint32_t key = port_lock();
    uint32_t best = 0;
    for (int32_t fl = FL_INDEX_MAX - 1; fl >= 0; fl--) {
        if (h->sl_bitmap[fl] == 0u) {
            continue;
        }
        uint16_t sl_map = h->sl_bitmap[fl];
        uint32_t sl = SL_INDEX_COUNT - 1;
        while (sl_map) {
            if (sl_map & (1u << sl)) {
                for (free_node_t *n = h->sl_head[fl][sl]; n != NULL; n = n->next) {
                    block_hdr_t *b = (block_hdr_t *)((uint8_t *)n - HDR_SIZE);
                    if (hdr_size(b) > best) {
                        best = hdr_size(b);
                    }
                }
                break;                     /* largest SL in this FL */
            }
            if (sl == 0) { break; }
            sl--;
        }
        if (best != 0u) {
            break;
        }
    }
    port_unlock(key);
    return best;
}

uint32_t sai_heap_fragmentation_pct(const sai_heap_t *h)
{
    uint32_t free_bytes = sai_heap_free_bytes(h);
    if (free_bytes == 0) {
        return 0;
    }
    uint32_t maxb = sai_heap_max_free_block(h);
    if (maxb >= free_bytes) {
        return 0;
    }
    /* 100% when the largest block vanishes relative to total free bytes */
    return 100u - (maxb * 100u) / free_bytes;
}
