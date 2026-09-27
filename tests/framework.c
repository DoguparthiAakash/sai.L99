/**
 * @file tests/framework.c
 * @brief Minimal host test framework: registration + runner.
 */
#include "framework.h"
#include <sai/host.h>

#define SAI_TEST_MAX 128

typedef struct {
    const char *name;
    void (*fn)(void);
} test_entry_t;

static test_entry_t s_tests[SAI_TEST_MAX];
static int s_test_registered;

int  sai_test_count = 0;
int  sai_test_failures = 0;
const char *sai_test_current = "";

void sai_test_register(const char *name, void (*fn)(void))
{
    if (s_test_registered < SAI_TEST_MAX) {
        s_tests[s_test_registered].name = name;
        s_tests[s_test_registered].fn = fn;
        s_test_registered++;
    }
}

int sai_test_main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);   /* survive crashes: see all output */
    printf("sai.L99 test suite: %d tests\n", s_test_registered);
    for (int i = 0; i < s_test_registered; i++) {
        sai_test_current = s_tests[i].name;
        sai_test_count = 0;
        sai_test_failures = 0;
        printf("[%s]\n", s_tests[i].name);
        s_tests[i].fn();
        if (sai_test_failures == 0) {
            printf("  ok (%d checks)\n", sai_test_count);
        } else {
            printf("  %d/%d checks FAILED\n", sai_test_failures, sai_test_count);
        }
    }
    printf("\n%d test(s), %d failure(s)\n", s_test_registered, sai_test_failures);
    return sai_test_failures == 0 ? 0 : 1;
}
