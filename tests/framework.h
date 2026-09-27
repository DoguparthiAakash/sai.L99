/**
 * @file tests/framework.h
 * @brief Minimal host test framework for sai.L99 kernel primitives.
 *
 * Test registration is compiler-portable: GCC/Clang use
 * __attribute__((constructor)); MSVC uses a function pointer placed in the
 * .CRT$XCU section, which the CRT initializer walker invokes before main().
 */
#ifndef SAI_TEST_FRAMEWORK_H
#define SAI_TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int  sai_test_count;
extern int  sai_test_failures;
extern const char *sai_test_current;

void sai_test_register(const char *name, void (*fn)(void));
int  sai_test_main(void);

#if defined(_MSC_VER)
/*
 * MSVC: user-defined initializers. The CRT invokes pointers in .CRT$XCU
 * between the .CRT$XCT and .CRT$XCZ ranges. We must fabricate a pointer
 * constant ourselves (MSVC has no __attribute__((constructor))).
 */
typedef void (*sai_test_ctor_fn)(void);
#pragma section(".CRT$XCU", read)
#define SAI_TEST_CTOR_I(fn, line)                                    \
    __declspec(allocate(".CRT$XCU"))                                 \
    static sai_test_ctor_fn sai_test_ctor_##line = fn;
#else
#define SAI_TEST_CTOR_I(fn, line)                                    \
    __attribute__((constructor)) static void sai_test_ctor_##line(void) { fn(); }
#endif
/* Extra level so __LINE__ (and token pastes) expand before use. */
#define SAI_TEST_CTOR_(fn, line) SAI_TEST_CTOR_I(fn, line)

#define SAI_TEST_CAT_(a, b)  a##b
#define SAI_TEST_CAT(a, b)   SAI_TEST_CAT_(a, b)

#define SAI_TEST_BEGIN(name)                                             \
    static void name(void);                                              \
    static void name##_run_ctor(void)                                    \
    {                                                                    \
        sai_test_register(#name, name);                                  \
    }                                                                    \
    SAI_TEST_CTOR_(name##_run_ctor, SAI_TEST_CAT(sai_l_, __LINE__))      \
    static void name(void)

#define SAI_CHECK(cond)                                                  \
    do {                                                                 \
        sai_test_count++;                                                \
        if (!(cond)) {                                                   \
            sai_test_failures++;                                         \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
        }                                                                \
    } while (0)

#define SAI_CHECK_EQ(a, b)                                               \
    do {                                                                 \
        sai_test_count++;                                                \
        long long _va = (long long)(a), _vb = (long long)(b);            \
        if (_va != _vb) {                                                \
            sai_test_failures++;                                         \
            printf("  FAIL %s:%d  %s (%lld) != %s (%lld)\n",             \
                   __FILE__, __LINE__, #a, _va, #b, _vb);                \
        }                                                                \
    } while (0)

#define SAI_TEST_END /* marker */

#endif /* SAI_TEST_FRAMEWORK_H */
