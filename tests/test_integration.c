/**
 * @file tests/test_integration.c
 * @brief End-to-end: producer -> msgq -> worker; simulated RX ISR -> mailbox
 *        -> shell thread. Proves multitasking, IPC and ISR signaling together.
 */
#include "framework.h"
#include <sai/kernel.h>
#include <sai/ipc.h>
#include <sai/time.h>
#include <sai/host.h>

static sai_msgq_t s_q;
static sai_mbox_t s_shellbox;
static sai_thread_t s_producer, s_worker, s_sheller;
static sai_event_t s_done;
static volatile uint32_t s_processed;
static volatile uint32_t s_shell_chars;

typedef struct { uint32_t seq; uint32_t value; } imsg_t;

static void producer(void *arg)
{
    (void)arg;
    for (uint32_t i = 1; i <= 10; i++) {
        imsg_t m = { .seq = i, .value = i * i };
        if (sai_msgq_put(&s_q, &m, 100) != SAI_OK) {
            break;
        }
        sai_sleep(2);
    }
}

static void worker(void *arg)
{
    (void)arg;
    imsg_t m;
    for (;;) {
        if (sai_msgq_get(&s_q, &m, 500) != SAI_OK) {
            break;
        }
        s_processed++;
        if (m.seq == 10u) {
            sai_event_set(&s_done, 0x1u);
        }
    }
}

static void sheller(void *arg)
{
    (void)arg;
    uint8_t c;
    for (;;) {
        if (sai_mbox_get(&s_shellbox, &c, 500) == SAI_OK) {
            s_shell_chars++;
        }
    }
}

static void fake_rx_isr(void *arg)
{
    (void)arg;
    static const char *line = "stats";
    static int idx;
    char c = line[idx];
    idx = (idx + 1) % 5;
    sai_isr_mbox_put(&s_shellbox, (uint8_t)c);
}

SAI_TEST_BEGIN(test_e2e_pipeline)
{
    SAI_CHECK_EQ(sai_msgq_init(&s_q, "iq", NULL, sizeof(imsg_t), 8), SAI_OK);
    SAI_CHECK_EQ(sai_mbox_init(&s_shellbox, "ishell", NULL, 32), SAI_OK);
    SAI_CHECK_EQ(sai_event_init(&s_done, "idone"), SAI_OK);

    s_processed = 0;
    s_shell_chars = 0;

    sai_thread_create(&s_producer, "prod", producer, NULL, 12, NULL, 2048, 0);
    sai_thread_create(&s_worker, "work", worker, NULL, 10, NULL, 2048, 0);
    sai_thread_create(&s_sheller, "shl", sheller, NULL, 14, NULL, 2048, 0);
    sai_thread_start(&s_worker);
    sai_thread_start(&s_producer);
    sai_thread_start(&s_sheller);

    sai_host_isr_register(7, fake_rx_isr, NULL);
    for (int i = 0; i < 5; i++) {
        sai_host_raise_isr(7);
        sai_sleep(3);
    }

    SAI_CHECK_EQ(sai_event_wait(&s_done, 0x1u, 0u, NULL, 2000), SAI_OK);
    SAI_CHECK_EQ(s_processed, 10u);
    SAI_CHECK(s_shell_chars >= 5u);

    sai_thread_delete(&s_worker);
    sai_thread_delete(&s_sheller);
    sai_msgq_destroy(&s_q);
    sai_mbox_destroy(&s_shellbox);
    sai_event_destroy(&s_done);
}
SAI_TEST_END
