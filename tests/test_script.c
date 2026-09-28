/**
 * @file tests/test_script.c
 * @brief Script service tests: sync/async execution, native language
 *        backends, registry lookups and error paths.
 */
#include "framework.h"
#include <sai/services.h>
#include <string.h>

SAI_TEST_BEGIN(test_lang_registry)
{
    /* all five languages registered by lazy init */
    sai_script_run(SAI_LANG_MICROPYTHON, "pass", 4, NULL, 0);

    for (int l = 0; l < (int)SAI_LANG_COUNT; l++) {
        sai_lang_backend_t *be = sai_lang_get((sai_lang_t)l);
        SAI_CHECK(be != NULL);
        SAI_CHECK_EQ(be->lang, l);
        SAI_CHECK(sai_lang_get_by_name(sai_lang_name((sai_lang_t)l)) == be);
    }
    SAI_CHECK(sai_lang_get_by_name("missing") == NULL);
    SAI_CHECK_EQ(sai_lang_from_name("rust"), SAI_LANG_RUST);
    SAI_CHECK_EQ(sai_lang_from_name("cpp"), SAI_LANG_CPP);
    SAI_CHECK_EQ(sai_lang_from_name("micropython"), SAI_LANG_MICROPYTHON);
}
SAI_TEST_END

SAI_TEST_BEGIN(test_script_sync_mp)
{
    sai_script_result_t res;
    static char outbuf[256];
    memset(&res, 0, sizeof(res));
    res.output = outbuf;
    res.output_cap = sizeof(outbuf);

    sai_status_t rc = sai_script_run(SAI_LANG_MICROPYTHON,
                                     "print('script svc ok')", 17, &res, 0);
    SAI_CHECK_EQ(rc, SAI_OK);
    SAI_CHECK_EQ(res.exit_code, 0);
    SAI_CHECK(strstr(res.output, "script svc ok") != NULL);
    SAI_CHECK_EQ(res.output_len, (uint32_t)strlen(res.output));
    SAI_CHECK(sai_script_runs() >= 1u);
}
SAI_TEST_END

SAI_TEST_BEGIN(test_script_async_mp)
{
    SAI_CHECK_EQ(sai_script_service_start(), SAI_OK);
    SAI_CHECK_EQ(sai_script_service_start(), SAI_OK);   /* idempotent */

    sai_status_t rc = sai_script_run_async(SAI_LANG_MICROPYTHON,
                                           "print('async done')", 19);
    SAI_CHECK_EQ(rc, SAI_OK);

    /* wait for completion (bounded) */
    uint32_t seq0 = sai_script_status(NULL, NULL);
    uint32_t spins = 0;
    while (sai_script_status(NULL, NULL) == seq0 && spins++ < 5000) {
        sai_sleep(2);
    }
    sai_lang_t lang = SAI_LANG_MICROPYTHON;
    int32_t code = -1;
    uint32_t seq = sai_script_status(&lang, &code);
    SAI_CHECK(seq != seq0);
    SAI_CHECK_EQ(lang, SAI_LANG_MICROPYTHON);
    SAI_CHECK_EQ(code, 0);

    SAI_CHECK_EQ(sai_script_service_stop(), SAI_OK);
}
SAI_TEST_END

SAI_TEST_BEGIN(test_script_native_c)
{
    /* host toolchain compiles + runs the snippet; exit code is its rc */
    SAI_CHECK_EQ(sai_lang_native_register("t42", SAI_LANG_C,
        "#include <stdio.h>\n"
        "int main(void){ printf(\"native42\\n\"); return 42; }\n",
        74), SAI_OK);

    sai_script_result_t res;
    static char outbuf[256];
    memset(&res, 0, sizeof(res));
    res.output = outbuf;
    res.output_cap = sizeof(outbuf);

    sai_status_t rc = sai_script_run(SAI_LANG_C, "t42", 3, &res, 0);
    SAI_CHECK_EQ(rc, SAI_OK);           /* backend ran the payload */
}
SAI_TEST_END

SAI_TEST_BEGIN(test_script_native_missing)
{
    sai_status_t rc = sai_script_run(SAI_LANG_C, "no_such_snippet", 15,
                                     NULL, 0);
    SAI_CHECK_EQ(rc, SAI_ERR_NOENT);
}
SAI_TEST_END

SAI_TEST_BEGIN(test_script_invalid_args)
{
    SAI_CHECK_EQ(sai_script_run(SAI_LANG_COUNT, "x", 1, NULL, 0),
                 SAI_ERR_INVAL);
    SAI_CHECK_EQ(sai_script_run(SAI_LANG_MICROPYTHON, NULL, 0, NULL, 0),
                 SAI_ERR_INVAL);
}
SAI_TEST_END
