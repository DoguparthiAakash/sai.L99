/**
 * @file samples/kernel_demo/main_target.c
 * @brief sai.L99 kernel demo (ARM target): 5 threads, msgq, sem, mutex,
 *        event flags, and a real SysTick-driven tick. Reports over USART2.
 */
#include <sai/kernel.h>
#include <sai/sync.h>
#include <sai/ipc.h>
#include <sai/time.h>
#include <sai/mem.h>
#include <sai/log.h>
#include <sai/console.h>

extern void sai_board_led_set(uint32_t idx, bool on);
extern sai_status_t sai_board_console_setup(void);

typedef struct { uint32_t seq; uint32_t value; } demo_msg_t;

static sai_msgq_t s_q;
static sai_mutex_t s_lock;
static sai_semaphore_t s_sem;
static sai_event_t s_ev;
static volatile uint32_t s_count, s_work;

static sai_thread_t t_prod, t_cons, t_work, t_rep;

static void producer(void *arg)
{
    (void)arg;
    uint32_t seq = 0;
    for (;;) {
        seq++;
        demo_msg_t m = { .seq = seq, .value = seq * 3u };
        (void)sai_msgq_put(&s_q, &m, SAI_WAIT_FOREVER);
        sai_sleep(20);
    }
}

static void consumer(void *arg)
{
    (void)arg;
    demo_msg_t m;
    for (;;) {
        if (sai_msgq_get(&s_q, &m, SAI_WAIT_FOREVER) == SAI_OK) {
            sai_mutex_lock(&s_lock);
            s_count++;
            sai_mutex_unlock(&s_lock);
            sai_sem_give(&s_sem);
            if ((s_count % 50u) == 0u) {
                sai_event_set(&s_ev, 0x1u);
            }
        }
    }
}

static void worker(void *arg)
{
    (void)arg;
    for (;;) {
        if (sai_sem_take(&s_sem) == SAI_OK) {
            sai_mutex_lock(&s_lock);
            s_work++;
            sai_mutex_unlock(&s_lock);
        }
    }
}

static void reporter(void *arg)
{
    (void)arg;
    for (;;) {
        sai_event_wait(&s_ev, 0x1u, SAI_EVENT_CONSUME, NULL, SAI_WAIT_FOREVER);
        sai_board_led_set(1, true);        /* green LED blinks each report */
        sai_printf("demo: msgs=%u work=%u tick=%u heap=%u\n",
                   (unsigned)s_count, (unsigned)s_work,
                   (unsigned)sai_tick_count(), (unsigned)sai_mem_free_bytes());
        sai_sleep(50);
        sai_board_led_set(1, false);
    }
}

/* Kernel main thread: set up the app, then park (other threads run). */
static void demo_main(void)
{
    sai_printf("\nsai.L99 %s kernel demo (cortex-m4)\n", SAI_VERSION_STRING);

    (void)sai_msgq_init(&s_q, "demoq", NULL, sizeof(demo_msg_t), 8);
    (void)sai_mutex_init(&s_lock, "demolock", false);
    (void)sai_sem_init(&s_sem, "demosem", 0, 0);
    (void)sai_event_init(&s_ev, "demoev");

    (void)sai_thread_create(&t_prod, "prod", producer, NULL, 12u, NULL, 1024, 0);
    (void)sai_thread_create(&t_cons, "cons", consumer, NULL, 10u, NULL, 1024, 0);
    (void)sai_thread_create(&t_work, "work", worker,   NULL, 11u, NULL, 1024, 0);
    (void)sai_thread_create(&t_rep,  "rep",  reporter, NULL, 8u,  NULL, 2048, 0);

    (void)sai_thread_start(&t_prod);
    (void)sai_thread_start(&t_cons);
    (void)sai_thread_start(&t_work);
    (void)sai_thread_start(&t_rep);

    for (;;) {
        sai_sleep(60000);              /* main just idles; workers run */
    }
}

int main(void)
{
    sai_console_init();

    if (sai_kernel_init() != SAI_OK) {
        sai_printf("FATAL: kernel init\n");
        return 1;
    }
    (void)sai_board_console_setup();

    sai_kernel_start(demo_main);       /* never returns */
    return 0;
}
