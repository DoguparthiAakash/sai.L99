/**
 * @file libc/stdlib.h
 * @brief Freestanding stdlib.h shim: abort, abs, div, bsearch, qsort.
 */
#ifndef __SAI_LIBC_STDLIB_H
#define __SAI_LIBC_STDLIB_H

#include <stddef.h>
#include <stdint.h>
#include <sai/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Halt the system (maps to sai_panic("abort")). */
SAI_NORETURN void abort(void);

int abs(int x);
long labs(long x);
long long llabs(long long x);

typedef struct { int quot, rem; } div_t;
typedef struct { long quot, rem; } ldiv_t;
typedef struct { long long quot, rem; } lldiv_t;
div_t div(int num, int den);
ldiv_t ldiv(long num, long den);
lldiv_t lldiv(long long num, long long den);

void qsort(void *base, size_t n, size_t size,
           int (*cmp)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t n, size_t size,
              int (*cmp)(const void *, const void *));

int atoi(const char *s);
long atol(const char *s);

/* Storage allocation.  Not implemented by this freestanding shim: on host
 * builds they resolve to the CRT, on targets to newlib or the board's
 * allocator.  They MUST be declared here so pointers are never truncated
 * by C89 implicit-int declarations. */
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif
