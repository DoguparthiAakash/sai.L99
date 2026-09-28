/**
 * @file tests/test_budget.c
 * @brief Latency-budget enforcement tests, driven by a fake clock so every
 *        scenario is deterministic on the host.
 */
#include "framework.h"
#include <sai/kernel.h>
#include <sai/budget.h>
#include <sai/ipc.h>

static uint32_t s_fake_tick = 1000;

static uint32_t fake_clock(void)
{
    return s_fake_tick;
}

static void clock_reset(void)
{
    s_fake_tick = 1000;
    sai_budget_set_clock(fake_clock);
}

SAI_TEST_BEGIN(test_budget_set_and_clear)
{
    clock_reset();
    sai_thread_t *main = sai_current_thread();
    SAI_CHECK_EQ(sai_budget_set(main, 100, 50, SAI_BUDGET_ACTION_LOG, 0), SAI_OK);
    /* invalid: allowance > window */
    SAI_CHECK_EQ(sai_budget_set(main, 50, 100, SAI_BUDGET_ACTION_LOG, 0),
                 SAI_ERR_INVAL);
    /* invalid: zero window */
    SAI_CHECK_EQ(sai_budget_set(main, 0, 0, SAI_BUDGET_ACTION_LOG, 0),
                 SAI_ERR_INVAL);

    sai_budget_stats_t st;
    SAI_CHECK_EQ(sai_budget_stats(main, &st), SAI_OK);
    SAI_CHECK_EQ(st.window_ticks, 100u);
    SAI_CHECK_EQ(st.allowance_ticks, 50u);
    SAI_CHECK_EQ(st.violations, 0u);

    SAI_CHECK_EQ(sai_budget_clear(main), SAI_OK);
    sai_budget_set_clock(NULL);
}
SAI_TEST_END

SAI_TEST_BEGIN(test_budget_log_action_counts_violation)
{
    clock_reset();
    sai_thread_t *main = sai_current_thread();
    SAI_CHECK_EQ(sai_budget_set(main, 100, 50, SAI_BUDGET_ACTION_LOG, 0), SAI_OK);

    /* Simulate 60 ticks of runtime within a 100-tick window: over budget. */
    for (uint32_t i = 0; i < 60; i++) {
        s_fake_tick++;
        _sai_budget_on_tick(main);
    }
    sai_budget_stats_t st;
    SAI_CHECK_EQ(sai_budget_stats(main, &st), SAI_OK);
    SAI_CHECK_EQ(st.violations, 1u);
    SAI_CHECK_EQ(st.used_ticks, 0u);       /* window restarted on action */
    SAI_CHECK_EQ(st.last_violation_at, 1060u);

    sai_budget_clear(main);
    sai_budget_set_clock(NULL);
}
SAI_TEST_END

SAI_TEST_BEGIN(test_budget_window_reset_no_false_positive)
{
    clock_reset();
    sai_thread_t *main = sai_current_thread();
    SAI_CHECK_EQ(sai_budget_set(main, 100, 50, SAI_BUDGET_ACTION_LOG, 0), SAI_OK);

    /* Run 60 ticks... */
    for (uint32_t i = 0; i < 60; i++) {
        s_fake_tick++;
        _sai_budget_on_tick(main);
    }
    /* ...then idle for a full window: the bucket must refill. */
    s_fake_tick += 100;
    for (uint32_t i = 0; i < 40; i++) {
        s_fake_tick++;
        _sai_budget_on_tick(main);
    }
    sai_budget_stats_t st;
    SAI_CHECK_EQ(sai_budget_stats(main, &st), SAI_OK);
    SAI_CHECK_EQ(st.violations, 1u);       /* no new violation */
    SAI_CHECK_EQ(st.used_ticks, 40u);      /* within fresh allowance */

    sai_budget_clear(main);
    sai_budget_set_clock(NULL);
}
SAI_TEST_END

SAI_TEST_BEGIN(test_budget_switch_out_accounting)
{
    clock_reset();
    sai_thread_t *main = sai_current_thread();
    SAI_CHECK_EQ(sai_budget_set(main, 100, 50, SAI_BUDGET_ACTION_LOG, 0), SAI_OK);

    /* Dispatch happened "at" tick 1000 (budget_run_start is only set by the
     * real scheduler; emulate it). */
    main->budget_run_start = s_fake_tick;
    s_fake_tick += 30;                     /* ran 30 ticks, then switched out */
    _sai_budget_on_switch_out(main);

    sai_budget_stats_t st;
    SAI_CHECK_EQ(sai_budget_stats(main, &st), SAI_OK);
    SAI_CHECK_EQ(st.used_ticks, 30u);
    SAI_CHECK_EQ(st.violations, 0u);

    main->budget_run_start = s_fake_tick;
    s_fake_tick += 30;                     /* 30 more: total 60 > 50 */
    _sai_budget_on_switch_out(main);
    SAI_CHECK_EQ(sai_budget_stats(main, &st), SAI_OK);
    SAI_CHECK_EQ(st.violations, 1u);

    sai_budget_clear(main);
    sai_budget_set_clock(NULL);
}
SAI_TEST_END

SAI_TEST_BEGIN(test_budget_violations_total)
{
    clock_reset();
    uint32_t before = sai_budget_violations_total();
    sai_thread_t *main = sai_current_thread();
    SAI_CHECK_EQ(sai_budget_set(main, 10, 5, SAI_BUDGET_ACTION_LOG, 0), SAI_OK);
    for (uint32_t i = 0; i < 8; i++) {
        s_fake_tick++;
        _sai_budget_on_tick(main);
    }
    SAI_CHECK(sai_budget_violations_total() >= before + 1u);
    sai_budget_clear(main);
    sai_budget_set_clock(NULL);
}
SAI_TEST_END
