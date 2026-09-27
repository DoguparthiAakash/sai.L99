/**
 * @file tests/test_ipc.c
 * @brief Message queue, mailbox, pipe and simulated-ISR signaling tests.
 */
#include "framework.h"
#include <sai/kernel.h>
#include <sai/ipc.h>
#include <sai/time.h>
#include <sai/host.h>

typedef struct {
    uint32_t seq;
    uint32_t payload;
} test_msg_t;

static sai_msgq_t s_q;
static sai_mbox_t s_mb;
static sai_pipe_t s_pp;
static sai_event_t s_ev;
static sai_thread_t s_consumer;
static volatile uint32_t s_last_seq;
static volatile uint32_t s_rx_count;

static void consumer_task(void *arg)
{
    (void)arg;
    test_msg_t m;
    for (;;) {
        if (sai_msgq_get(&s_q, &m, SAI_WAIT_FOREVER) == SAI_OK) {
            s_last_seq = m.seq;
            s_rx_count++;
            if (m.seq == 5u) {
                sai_event_set(&s_ev, 0x1u);
            }
        }
    }
}

SAI_TEST_BEGIN(test_msgq_thread_ipc)
{
    sai_msgq_init(&s_q, "q1", NULL, sizeof(test_msg_t), 8);
    sai_event_init(&s_ev, "ev1");
    s_rx_count = 0;
    s_last_seq = 0;

    sai_thread_create(&s_consumer, "cons", consumer_task, NULL, 10, NULL, 2048, 0);
    sai_thread_start(&s_consumer);

    for (uint32_t i = 1; i <= 5; i++) {
        test_msg_t m = { .seq = i, .payload = i * 10u };
        SAI_CHECK_EQ(sai_msgq_put(&s_q, &m, 100), SAI_OK);
        sai_sleep(5);
    }
    SAI_CHECK_EQ(sai_event_wait(&s_ev, 0x1u, 0u, NULL, 500), SAI_OK);
    SAI_CHECK_EQ(s_last_seq, 5u);
    SAI_CHECK_EQ(s_rx_count, 5u);
    sai_msgq_destroy(&s_q);
    sai_event_destroy(&s_ev);
}
SAI_TEST_END

SAI_TEST_BEGIN(test_msgq_overwrite)
{
    test_msg_t m;
    sai_msgq_init(&s_q, "q2", NULL, sizeof(test_msg_t), 2);
    for (uint32_t i = 0; i < 5; i++) {
        m.seq = i;
        SAI_CHECK_EQ(sai_msgq_put_overwrite(&s_q, &m), SAI_OK);
    }
    SAI_CHECK_EQ(sai_msgq_count(&s_q), 2u);
    SAI_CHECK_EQ(sai_msgq_get(&s_q, &m, SAI_NO_WAIT), SAI_OK);
    SAI_CHECK_EQ(m.seq, 3u);              /* oldest two were dropped */
    SAI_CHECK_EQ(sai_msgq_get(&s_q, &m, SAI_NO_WAIT), SAI_OK);
    SAI_CHECK_EQ(m.seq, 4u);
    sai_msgq_destroy(&s_q);
}
SAI_TEST_END

SAI_TEST_BEGIN(test_msgq_isr_put)
{
    test_msg_t m;
    sai_msgq_init(&s_q, "q3", NULL, sizeof(test_msg_t), 4);
    for (uint32_t i = 0; i < 4; i++) {
        m.seq = i;
        SAI_CHECK_EQ(sai_isr_msgq_put(&s_q, &m), SAI_OK);   /* never blocks */
    }
    m.seq = 99;
    SAI_CHECK_EQ(sai_isr_msgq_put(&s_q, &m), SAI_ERR_FULL);
    for (uint32_t i = 0; i < 4; i++) {
        SAI_CHECK_EQ(sai_msgq_get(&s_q, &m, SAI_NO_WAIT), SAI_OK);
        SAI_CHECK_EQ(m.seq, i);
    }
    sai_msgq_destroy(&s_q);
}
SAI_TEST_END

SAI_TEST_BEGIN(test_mbox)
{
    sai_mbox_init(&s_mb, "mb1", NULL, 4);
    SAI_CHECK_EQ(sai_mbox_put(&s_mb, 'a', SAI_NO_WAIT), SAI_OK);
    SAI_CHECK_EQ(sai_mbox_put(&s_mb, 'b', SAI_NO_WAIT), SAI_OK);
    SAI_CHECK_EQ(sai_isr_mbox_put(&s_mb, 'c'), SAI_OK);
    uint8_t c;
    SAI_CHECK_EQ(sai_mbox_get(&s_mb, &c, SAI_NO_WAIT), SAI_OK);
    SAI_CHECK_EQ(c, 'a');
    SAI_CHECK_EQ(sai_mbox_get(&s_mb, &c, SAI_NO_WAIT), SAI_OK);
    SAI_CHECK_EQ(c, 'b');
    SAI_CHECK_EQ(sai_mbox_get(&s_mb, &c, SAI_NO_WAIT), SAI_OK);
    SAI_CHECK_EQ(c, 'c');
    SAI_CHECK_EQ(sai_mbox_get(&s_mb, &c, SAI_NO_WAIT), SAI_ERR_EMPTY);
    sai_mbox_destroy(&s_mb);
}
SAI_TEST_END

SAI_TEST_BEGIN(test_pipe)
{
    sai_pipe_init(&s_pp, "p1", NULL, 8);
    const char *msg = "hello pipe";
    int32_t n = sai_pipe_write(&s_pp, msg, 10, SAI_NO_WAIT);
    SAI_CHECK(n > 0);
    char buf[16];
    memset(buf, 0, sizeof(buf));
    n = sai_pipe_read(&s_pp, buf, sizeof(buf), SAI_NO_WAIT);
    SAI_CHECK(n > 0);
    SAI_CHECK(memcmp(buf, msg, (size_t)n) == 0);
    sai_pipe_destroy(&s_pp);
}
SAI_TEST_END
