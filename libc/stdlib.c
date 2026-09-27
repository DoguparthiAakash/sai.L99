/**
 * @file libc/stdlib.c
 * @brief Freestanding stdlib routines.
 */
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sai/log.h>                   /* sai_panic (abort) */

SAI_NORETURN void abort(void)
{
    sai_panic("abort", __FILE__, __LINE__);
}

int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }
long long llabs(long long x) { return x < 0 ? -x : x; }

div_t div(int num, int den)
{
    div_t r;
    r.quot = num / den;
    r.rem = num - r.quot * den;
    return r;
}

ldiv_t ldiv(long num, long den)
{
    ldiv_t r;
    r.quot = num / den;
    r.rem = num - r.quot * den;
    return r;
}

lldiv_t lldiv(long long num, long long den)
{
    lldiv_t r;
    r.quot = num / den;
    r.rem = num - r.quot * den;
    return r;
}

void qsort(void *base, size_t n, size_t size,
           int (*cmp)(const void *, const void *))
{
    uint8_t *b = base;
    /* insertion sort: fine for the small arrays a kernel sorts */
    for (size_t i = 1; i < n; i++) {
        uint8_t tmp[64];
        if (size > sizeof(tmp)) {
            return;                      /* refuse huge elements */
        }
        memcpy(tmp, b + i * size, size);
        size_t j = i;
        while (j > 0 && cmp(b + (j - 1u) * size, tmp) > 0) {
            memcpy(b + j * size, b + (j - 1u) * size, size);
            j--;
        }
        memcpy(b + j * size, tmp, size);
    }
}

void *bsearch(const void *key, const void *base, size_t n, size_t size,
              int (*cmp)(const void *, const void *))
{
    const uint8_t *b = base;
    while (n > 0) {
        size_t mid = n / 2u;
        const void *p = b + mid * size;
        int c = cmp(key, p);
        if (c == 0) {
            return (void *)p;
        }
        if (c > 0) {
            b = (const uint8_t *)p + size;
            n -= mid + 1u;
        } else {
            n = mid;
        }
    }
    return NULL;
}

int atoi(const char *s)
{
    int r = 0;
    bool neg = false;
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    if (*s == '-') { neg = true; s++; }
    else if (*s == '+') { s++; }
    while (*s >= '0' && *s <= '9') {
        r = r * 10 + (*s - '0');
        s++;
    }
    return neg ? -r : r;
}

long atol(const char *s)
{
    long r = 0;
    bool neg = false;
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    if (*s == '-') { neg = true; s++; }
    else if (*s == '+') { s++; }
    while (*s >= '0' && *s <= '9') {
        r = r * 10 + (*s - '0');
        s++;
    }
    return neg ? -r : r;
}
