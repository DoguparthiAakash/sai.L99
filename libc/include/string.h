/**
 * @file libc/string.h
 * @brief Freestanding string.h shim.
 */
#ifndef __SAI_LIBC_STRING_H
#define __SAI_LIBC_STRING_H

#include <stddef.h>

#ifndef SAI_RESTRICT
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define SAI_RESTRICT restrict
#elif defined(_MSC_VER)
#define SAI_RESTRICT __restrict
#else
#define SAI_RESTRICT
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

void *memset(void *dst, int c, size_t n);
void *memcpy(void *SAI_RESTRICT dst, const void *SAI_RESTRICT src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
size_t strnlen(const char *s, size_t maxlen);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strcat(char *dst, const char *src);
char *strncat(char *dst, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
int   strcmp(const char *a, const char *b);
int   strncmp(const char *a, const char *b, size_t n);
char *strstr(const char *h, const char *n);
char *strdup_s(char *dst, size_t dstsz, const char *src);

#ifdef __cplusplus
}
#endif

#endif
