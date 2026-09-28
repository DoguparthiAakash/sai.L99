/**
 * @file tests/test_services.c
 * @brief Service framework, events pub/sub and softrpc registry tests.
 */
#include "framework.h"
#include <sai/services.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* service framework                                                   */
/* ------------------------------------------------------------------ */

static sai_service_t *s_svc;
static volatile uint32_t s_received;
static volatile bool s_got_stop;

static void echo_service(sai_service_t *svc)
{
    for (;;) {
        uint32_t msg;
        sai_status_t rc = sai_service_recv(svc, &msg, SAI_WAIT_FOREVER);
        if (rc == SAI_ERR_STATE) {
            s_got_stop = true;
            return;
        }
        if (rc == SAI_OK) {
            s_received = msg;
        }
    }
}

SAI_TEST_BEGIN(test_service_lifecycle)
{
    s_received = 0;
    s_got_stop = false;
    SAI_CHECK_EQ(sai_service_start("echo", echo_service, NULL, 2048, 15), SAI_OK);
    s_svc = sai_service_find("echo");
    SAI_CHECK(s_svc != NULL);
    SAI_CHECK(strcmp(sai_service_name(s_svc), "echo") == 0);

    /* duplicate start rejected */
    SAI_CHECK_EQ(sai_service_start("echo", echo_service, NULL, 2048, 15),
                 SAI_ERR_NOENT);

    /* post + receive (sleep between checks: same-priority yields do not
     * switch threads, so polling must give the service CPU time) */
    for (uint32_t i = 1; i <= 3; i++) {
        SAI_CHECK_EQ(sai_service_post(s_svc, i), SAI_OK);
        s_received = 0;
        uint32_t spins = 0;
        while (s_received != i && spins++ < 500) {
            sai_sleep(2);
        }
        SAI_CHECK_EQ(s_received, i);
    }

    /* stop: synchronous, loop must observe SAI_ERR_STATE and exit */
    SAI_CHECK_EQ(sai_service_stop(s_svc), SAI_OK);
    SAI_CHECK(s_got_stop);
    SAI_CHECK(sai_service_is_stopped(s_svc));
}
SAI_TEST_END

/* ------------------------------------------------------------------ */
/* events pub/sub                                                      */
/* ------------------------------------------------------------------ */

SAI_TEST_BEGIN(test_events_pub_sub)
{
    sai_events_reset();
    sai_event_t *ev = NULL;
    SAI_CHECK_EQ(sai_events_open("chan", &ev), SAI_OK);
    SAI_CHECK(ev != NULL);

    /* open of an existing name returns the same channel */
    sai_event_t *ev2 = NULL;
    SAI_CHECK_EQ(sai_events_open("chan", &ev2), SAI_OK);
    SAI_CHECK(ev2 == ev);

    /* publish -> wait (already-set flags, so no block) */
    SAI_CHECK_EQ(sai_events_publish("chan", 0x4u), SAI_OK);
    uint32_t got = 0;
    SAI_CHECK_EQ(sai_events_wait("chan", 0x4u, SAI_EVENT_WAIT_ANY | SAI_EVENT_CONSUME,
                                 &got, 100), SAI_OK);
    SAI_CHECK_EQ(got, 0x4u);

    /* publish on unknown channel */
    SAI_CHECK_EQ(sai_events_publish("nope", 0x1u), SAI_ERR_NOENT);

    /* unknown pool exhaustion: SAI_EVENTS_MAX_CHANNELS slots */
    for (int i = 0; i < SAI_EVENTS_MAX_CHANNELS - 1; i++) {
        sai_event_t *tmp = NULL;
        char name[8];
        sai_snprintf(name, sizeof(name), "ch%d", i);
        SAI_CHECK_EQ(sai_events_open(name, &tmp), SAI_OK);
    }
    sai_event_t *overflow = NULL;
    SAI_CHECK_EQ(sai_events_open("overflow", &overflow), SAI_ERR_FULL);

    sai_events_reset();
    /* after reset, channels are available again */
    SAI_CHECK_EQ(sai_events_open("chan", &ev), SAI_OK);
    sai_events_reset();
}
SAI_TEST_END

/* ------------------------------------------------------------------ */
/* softrpc                                                             */
/* ------------------------------------------------------------------ */

static int rpc_add(uint32_t a0, uint32_t a1, int32_t *ret)
{
    if (ret != NULL) {
        *ret = (int32_t)(a0 + a1);
    }
    return SAI_OK;
}

static int rpc_fail(uint32_t a0, uint32_t a1, int32_t *ret)
{
    (void)a0; (void)a1; (void)ret;
    return SAI_ERR_IO;
}

SAI_TEST_BEGIN(test_softrpc_registry)
{
    static const sai_softrpc_method_t add = {
        .name = "add", .id = 1, .handler = rpc_add,
    };
    static const sai_softrpc_method_t fail = {
        .name = "fail", .id = 2, .handler = rpc_fail,
    };
    SAI_CHECK_EQ(sai_softrpc_register(&add), SAI_OK);
    SAI_CHECK_EQ(sai_softrpc_register(&add), SAI_ERR_NOENT);  /* duplicate */
    SAI_CHECK_EQ(sai_softrpc_register(&fail), SAI_OK);

    int32_t ret = 0;
    SAI_CHECK_EQ(sai_softrpc_call("add", 40, 2, &ret), SAI_OK);
    SAI_CHECK_EQ(ret, 42);

    SAI_CHECK_EQ(sai_softrpc_call("fail", 0, 0, &ret), SAI_ERR_IO);
    SAI_CHECK_EQ(sai_softrpc_call("missing", 0, 0, &ret), SAI_ERR_NOENT);

    const sai_softrpc_method_t *m = sai_softrpc_find("add");
    SAI_CHECK(m != NULL);
    SAI_CHECK_EQ(m->id, 1);
}
SAI_TEST_END

/* ------------------------------------------------------------------ */
/* stats service                                                       */
/* ------------------------------------------------------------------ */

SAI_TEST_BEGIN(test_stats_snapshot)
{
    sai_stats_snapshot_t s;
    sai_stats_get(&s);
    SAI_CHECK(s.threads >= 2u);         /* at least main + idle */
    SAI_CHECK(s.kobjects >= s.threads);
    SAI_CHECK(s.heap_free > 0u);

    sai_stats_print(&s);                /* smoke: must not crash */

    /* heartbeat daemon start/stop */
    SAI_CHECK_EQ(sai_stats_service_start(50u), SAI_OK);
    sai_sleep(150);
    SAI_CHECK_EQ(sai_stats_service_stop(), SAI_OK);
}
SAI_TEST_END
