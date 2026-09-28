/**
 * @file tests/test_micropython.c
 * @brief End-to-end MicroPython tests: init, exec, print capture, errors.
 *
 * Every exec runs on the kernel main thread (test_main.c), which is what
 * the lazy VM init expects: the stack anchor must be established before
 * other threads ever touch the VM.
 */
#include "framework.h"
#include <sai/services.h>
#include <sai/console.h>
#include <string.h>

static void run_mp(const char *src, sai_script_result_t *res)
{
    static char outbuf[512];
    memset(res, 0, sizeof(*res));
    res->output = outbuf;
    res->output_cap = sizeof(outbuf);
    sai_status_t rc = sai_script_run(SAI_LANG_MICROPYTHON, src,
                                     strlen(src), res, 0);
    res->exit_code = (rc == SAI_OK) ? 0 : rc;
}

SAI_TEST_BEGIN(test_mp_hello)
{
    sai_script_result_t res;
    run_mp("print('hello sai')", &res);
    SAI_CHECK_EQ(res.exit_code, 0);
    SAI_CHECK(res.output_len > 0);
    SAI_CHECK(strstr(res.output, "hello sai") != NULL);
}
SAI_TEST_END

SAI_TEST_BEGIN(test_mp_arith_and_vars)
{
    sai_script_result_t res;
    run_mp("x = 6 * 7\nprint('x =', x)", &res);
    SAI_CHECK_EQ(res.exit_code, 0);
    SAI_CHECK(strstr(res.output, "x = 42") != NULL);
}
SAI_TEST_END

SAI_TEST_BEGIN(test_mp_state_persists)
{
    sai_script_result_t res;
    run_mp("counter = 123", &res);
    SAI_CHECK_EQ(res.exit_code, 0);
    run_mp("print('counter is', counter)", &res);
    SAI_CHECK_EQ(res.exit_code, 0);
    SAI_CHECK(strstr(res.output, "counter is 123") != NULL);
}
SAI_TEST_END

SAI_TEST_BEGIN(test_mp_syntax_error_reported)
{
    sai_script_result_t res;
    run_mp("def broken(:\n", &res);
    SAI_CHECK(res.exit_code != 0);
    /* No output capture on failure, but the run must not hang or crash. */
}
SAI_TEST_END

SAI_TEST_BEGIN(test_mp_runtime_error_reported)
{
    sai_script_result_t res;
    run_mp("raise ValueError('boom')", &res);
    SAI_CHECK(res.exit_code != 0);
    /* VM must still be usable afterwards (backend re-inits after errors). */
    run_mp("print('recovered')", &res);
    SAI_CHECK_EQ(res.exit_code, 0);
    SAI_CHECK(strstr(res.output, "recovered") != NULL);
}
SAI_TEST_END

SAI_TEST_BEGIN(test_mp_sai_module)
{
    sai_script_result_t res;
    run_mp("import sai\nprint('ver', sai.version())", &res);
    SAI_CHECK_EQ(res.exit_code, 0);
    SAI_CHECK(strstr(res.output, "ver") != NULL);
}
SAI_TEST_END
