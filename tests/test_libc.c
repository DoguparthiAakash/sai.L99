/**
 * @file tests/test_libc.c
 * @brief libc shim tests: string ops and printf formatting.
 */
#include "framework.h"
#include <sai/console.h>
#include <string.h>

SAI_TEST_BEGIN(test_string)
{
    char buf[64];
    SAI_CHECK_EQ(strlen("sai.L99"), 7u);
    SAI_CHECK_EQ(strcmp("abc", "abc"), 0);
    SAI_CHECK(strcmp("abc", "abd") < 0);
    SAI_CHECK(strcmp("ab", "abc") < 0);

    memset(buf, 0, sizeof(buf));
    SAI_CHECK(strcpy(buf, "hello") == buf);
    SAI_CHECK_EQ(strcmp(buf, "hello"), 0);
    SAI_CHECK(strcat(buf, " world") == buf);
    SAI_CHECK_EQ(strcmp(buf, "hello world"), 0);

    SAI_CHECK(strchr(buf, 'w') == buf + 6);
    SAI_CHECK(strchr(buf, 'z') == NULL);
    SAI_CHECK(strrchr(buf, 'o') == buf + 7);

    SAI_CHECK_EQ(strncmp("abcdef", "abcxyz", 3), 0);
    SAI_CHECK(strncmp("abcdef", "abcxyz", 4) < 0);

    SAI_CHECK(strstr("hello world", "lo w") != NULL);
    SAI_CHECK(strstr("hello world", "xyz") == NULL);

    memset(buf, 0xEE, sizeof(buf));
    SAI_CHECK(memcmp(memset(buf, 0, 8), "\0\0\0\0\0\0\0\0", 8) == 0);

    uint8_t overlap[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    memmove(overlap + 2, overlap, 4);      /* overlapping forward */
    SAI_CHECK_EQ(overlap[2], 1);
    SAI_CHECK_EQ(overlap[5], 4);
}
SAI_TEST_END

SAI_TEST_BEGIN(test_snprintf)
{
    char buf[64];
    SAI_CHECK_EQ(sai_snprintf(buf, sizeof(buf), "%d", -42), 3);
    SAI_CHECK_EQ(strcmp(buf, "-42"), 0);

    sai_snprintf(buf, sizeof(buf), "%u %x %X", 4294967295u, 0xabcdu, 0xABCDu);
    SAI_CHECK_EQ(strcmp(buf, "4294967295 abcd ABCD"), 0);

    sai_snprintf(buf, sizeof(buf), "%5d|%-5d|%05d", 42, 42, 42);
    SAI_CHECK_EQ(strcmp(buf, "   42|42   |00042"), 0);

    sai_snprintf(buf, sizeof(buf), "%s=%d", "temp", 21);
    SAI_CHECK_EQ(strcmp(buf, "temp=21"), 0);

    sai_snprintf(buf, sizeof(buf), "%c%c", 'o', 'k');
    SAI_CHECK_EQ(strcmp(buf, "ok"), 0);

    sai_snprintf(buf, sizeof(buf), "100%% %p", (void *)0x1234);
    SAI_CHECK(strstr(buf, "100% 0x1234") == buf);

    /* truncation is safe */
    char small[8];
    int32_t n = sai_snprintf(small, sizeof(small), "%s", "truncated-string");
    SAI_CHECK_EQ(n, 16);                   /* full length returned */
    SAI_CHECK_EQ(strcmp(small, "truncat"), 0);
}
SAI_TEST_END

static int cmp_int(const void *a, const void *b)
{
    return *(const int *)a - *(const int *)b;
}

SAI_TEST_BEGIN(test_stdlib)
{
    SAI_CHECK_EQ(abs(-5), 5);
    div_t d = div(17, 5);
    SAI_CHECK_EQ(d.quot, 3);
    SAI_CHECK_EQ(d.rem, 2);

    int arr[] = { 5, 2, 9, 1, 7 };
    qsort(arr, 5, sizeof(int), cmp_int);
    SAI_CHECK_EQ(arr[0], 1);
    SAI_CHECK_EQ(arr[4], 9);

    int key = 7;
    int *found = bsearch(&key, arr, 5, sizeof(int), cmp_int);
    SAI_CHECK(found != NULL && *found == 7);

    SAI_CHECK_EQ(atoi("42"), 42);
    SAI_CHECK_EQ(atoi("-17"), -17);
    SAI_CHECK_EQ(atol("123456"), 123456L);
}
SAI_TEST_END
