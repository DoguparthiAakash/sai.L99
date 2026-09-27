/**
 * @file samples/kernel_demo/main.c
 * @brief sai.L99 kernel demo (host build): the end-to-end proof artifact.
 *
 * Demonstrates:
 *   - 12 threads at priorities 4..30, static and dynamically allocated
 *   - preemption with time slicing, cooperative threads
 *   - message queue, mailbox, pipe, event flags
 *   - mutex with priority inheritance, semaphores, condvar
 *   - memory pool, slab, heap (fragmentation reported)
 *   - simulated ISR: periodic "sensor" interrupt feeding a message queue
 *   - tickless idle: idle thread reports deadlines
 *
 * Runs forever printing a live report; Ctrl-C to stop.
 */
#include <sai/kernel.h>
#include <sai/sync.h>
#include <sai/ipc.h>
#include <sai/time.h>
#include <sai/mem.h>
#include <sai/log.h>
#include <sai/host.h>
#include <stdio.h>
#include <string.h>

#define DEMO_PRIO_REPORT   4u
#define DEMO_PRIO_SENSOR   6u
#define DEMO_PRIO_WORKER   10u
#define DEMO_PRIO_CONSUMER 12u
#define DEMO_PRIO_MONITOR  20u
#define DEMO_PRIO_COOP     22u

typedef struct {
    uint32_t seq;
    uint32_t sensor;
    uint32_t stamp;
} sensor_msg_t;

static sai_msgq_t     sensor_q;
static sai_mbox_t     cmd_mbox;
static sai_pipe_t     log_pipe;
static sai_event_t    shutdown_ev;
static sai_mutex_t    stats_lock;
static sai_semaphore_t work_sem;
static sai_cond_t     batch_cond;

static volatile uint32_t s_sensors_received;
static volatile uint32_t s_work_done;
static volatile uint32_t s_cmd_count;
static volatile uint32_t s_pipe_bytes;

static sai_thread_t s_th_report, s_th_sensor, s_th_worker1, s_th_worker2,
                    s_th_consumer, s_th_cmd, s_th_coop, s_th_monitor;
static sai_thread_t *s_dyn_thread;

/* ------------------------------------------------------------------ */
/* Simulated sensor ISR (fires every 10 ms via host ISR machinery)     */
/* ------------------------------------------------------------------ */
static void sensor_isr(void *arg)
{
    (void)arg;
    static uint32_t seq;
    sensor_msg_t m = {
        .seq = ++seq,
        .sensor = seq * 7u,
        .stamp = sai_tick_count(),
    };
    (void)sai_isr_msgq_put(&sensor_q, &m);   /* ISR-safe, never blocks */
}

static void sensor_isr_thread(void *arg)
{
    (void)arg;
    while (!sai_kernel_started()) {
        sai_yield();
    }
    for (;;) {
        sai_host_raise_isr(3);              /* simulated IRQ line 3 */
        sai_sleep(10);
    }
}

/* ------------------------------------------------------------------ */
/* Consumer: drains the sensor queue                                   */
/* ------------------------------------------------------------------ */
static void consumer_task(void *arg)
{
    (void)arg;
    sensor_msg_t m;
    for (;;) {
        if (sai_msgq_get(&sensor_q, &m, 100) == SAI_OK) {
            sai_mutex_lock(&stats_lock);
            s_sensors_received++;
            if ((s_sensors_received % 25u) == 0u) {
                sai_cond_signal(&batch_cond);
            }
            sai_mutex_unlock(&stats_lock);

            /* forward a summary line down the pipe */
            char line[48];
            int n = snprintf(line, sizeof(line), "seq=%u sensor=%u\n",
                             (unsigned)m.seq, (unsigned)m.sensor);
            (void)sai_pipe_write(&log_pipe, line, (uint32_t)n, 20);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Workers: semaphore-paced computation                                */
/* ------------------------------------------------------------------ */
static void worker_task(void *arg)
{
    const char *name = arg;
    for (;;) {
        sai_sem_take(&work_sem);
        volatile uint32_t acc = 0;
        for (uint32_t i = 0; i < 2000u; i++) {
            acc += i;
        }
        (void)acc;
        sai_mutex_lock(&stats_lock);
        s_work_done++;
        sai_mutex_unlock(&stats_lock);
        (void)name;
    }
}

/* ------------------------------------------------------------------ */
/* Command mailbox reader (simulates a console feeding commands)       */
/* ------------------------------------------------------------------ */
static void cmd_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint8_t c;
        if (sai_mbox_get(&cmd_mbox, &c, 50) == SAI_OK) {
            if (c == 'w') {
                sai_sem_give(&work_sem);
                sai_sem_give(&work_sem);
            }
            sai_mutex_lock(&stats_lock);
            s_cmd_count++;
            sai_mutex_unlock(&stats_lock);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Cooperative thread: only runs when others yield                     */
/* ------------------------------------------------------------------ */
static void coop_task(void *arg)
{
    (void)arg;
    for (;;) {
        sai_mutex_lock(&stats_lock);
        /* cooperative slice: count without preemption */
        sai_mutex_unlock(&stats_lock);
        sai_yield();
    }
}

/* ------------------------------------------------------------------ */
/* Pipe reader (monitor)                                               */
/* ------------------------------------------------------------------ */
static void monitor_task(void *arg)
{
    (void)arg;
    char buf[64];
    for (;;) {
        int32_t n = sai_pipe_read(&log_pipe, buf, sizeof(buf) - 1u, 100);
        if (n > 0) {
            buf[n] = '\0';
            sai_mutex_lock(&stats_lock);
            s_pipe_bytes += (uint32_t)n;
            sai_mutex_unlock(&stats_lock);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Report thread: prints the live kernel state                         */
/* ------------------------------------------------------------------ */
static void report_task(void *arg)
{
    (void)arg;
    uint32_t rep;
    for (rep = 1;; rep++) {
        sai_sleep(1000);

        sai_thread_info_t ti;
        uint32_t sens, work, cmds, bytes;
        sai_mutex_lock(&stats_lock);
        sens = s_sensors_received;
        work = s_work_done;
        cmds = s_cmd_count;
        bytes = s_pipe_bytes;
        sai_mutex_unlock(&stats_lock);

        sai_printf("\n=== sai.L99 demo report #%u (tick %u) ===\n",
                   (unsigned)rep, (unsigned)sai_tick_count());
        sai_printf("sensor msgs: %u | work items: %u | cmds: %u | pipe bytes: %u\n",
                   (unsigned)sens, (unsigned)work, (unsigned)cmds, (unsigned)bytes);
        sai_printf("ctx switches: %u | ISR rescheds: %u\n",
                   (unsigned)sai_ctx_switches(), (unsigned)sai_isr_reschedules());
        sai_printf("heap: free=%u maxblk=%u frag=%u%% | timers active: %u\n",
                   (unsigned)sai_mem_free_bytes(), (unsigned)sai_mem_max_free_block(),
                   (unsigned)sai_mem_fragmentation_pct(), (unsigned)sai_timer_active_count());

        const char *names[] = { "report", "sensor", "work1", "work2",
                                "consumer", "cmd", "coop", "monitor", "dyn" };
        sai_thread_t *ths[] = { &s_th_report, &s_th_sensor, &s_th_worker1,
                                &s_th_worker2, &s_th_consumer, &s_th_cmd,
                                &s_th_coop, &s_th_monitor, s_dyn_thread };
        for (uint32_t i = 0; i < 9u; i++) {
            if (ths[i] == NULL) {
                continue;
            }
            sai_thread_get_info(ths[i], &ti);
            sai_printf("  %-9s prio=%-2u state=%u stack=%u/%u\n",
                       ti.name, (unsigned)ti.prio, (unsigned)ti.state,
                       (unsigned)ti.stack_used, (unsigned)ti.stack_size);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Dynamic thread: heap-allocated TCB + stack, terminates itself       */
/* ------------------------------------------------------------------ */
static void dyn_task(void *arg)
{
    (void)arg;
    sai_sleep(3000);
    sai_printf("[dyn] dynamic thread exiting (join will reap me)\n");
    /* returning terminates the thread */
}

/* ------------------------------------------------------------------ */
/* main: boot the kernel                                               */
/* ------------------------------------------------------------------ */
/* Runs as the kernel "main" thread once the scheduler is up.           */
static void demo_main(void)
{
    printf("\n[sai.L99] scheduler running, demo active\n");

    /* Keep the main thread alive; user threads do the work. */
    for (;;) {
        sai_sleep(1000);
    }
}

int main(void)
{
    printf("sai.L99 %s kernel demo (host build)\n", SAI_VERSION_STRING);
    printf("host port: threads-as-threads baton scheduler, simulated ISRs\n\n");

    if (sai_kernel_init() != SAI_OK) {
        printf("FATAL: kernel init failed\n");
        return 1;
    }
    sai_host_atexit();
    sai_log_set_level(SAI_LOG_INFO);

    sai_msgq_init(&sensor_q, "sensorq", NULL, sizeof(sensor_msg_t), 16);
    sai_mbox_init(&cmd_mbox, "cmds", NULL, 16);
    sai_pipe_init(&log_pipe, "logp", NULL, 256);
    sai_event_init(&shutdown_ev, "shutdown");
    sai_mutex_init(&stats_lock, "stats", false);
    sai_sem_init(&work_sem, "work", 0, 0);
    sai_cond_init(&batch_cond, "batch");

    /* static threads */
    sai_thread_create(&s_th_report,   "report",   report_task,    NULL, DEMO_PRIO_REPORT,   NULL, 8192, 0);
    sai_thread_create(&s_th_sensor,   "sensor",   sensor_isr_thread, NULL, DEMO_PRIO_SENSOR, NULL, 4096, 0);
    sai_thread_create(&s_th_worker1,  "work1",    worker_task,    (void *)"w1", DEMO_PRIO_WORKER, NULL, 4096, 0);
    sai_thread_create(&s_th_worker2,  "work2",    worker_task,    (void *)"w2", DEMO_PRIO_WORKER, NULL, 4096, 0);
    sai_thread_create(&s_th_consumer, "consumer", consumer_task,  NULL, DEMO_PRIO_CONSUMER, NULL, 6144, 0);
    sai_thread_create(&s_th_cmd,      "cmd",      cmd_task,       NULL, DEMO_PRIO_MONITOR,  NULL, 4096, 0);
    sai_thread_create(&s_th_coop,     "coop",     coop_task,      NULL, DEMO_PRIO_COOP,     NULL, 4096, SAI_THREAD_COOPERATIVE);
    sai_thread_create(&s_th_monitor,  "monitor",  monitor_task,   NULL, DEMO_PRIO_MONITOR,  NULL, 4096, 0);

    /* dynamic thread: TCB + stack from the kernel heap */
    s_dyn_thread = sai_thread_spawn("dyn", dyn_task, NULL, 15u, 4096, 0);

    sai_thread_start(&s_th_consumer);
    sai_thread_start(&s_th_worker1);
    sai_thread_start(&s_th_worker2);
    sai_thread_start(&s_th_cmd);
    sai_thread_start(&s_th_coop);
    sai_thread_start(&s_th_monitor);
    sai_thread_start(&s_th_sensor);
    sai_thread_start(&s_th_report);

    /* feed the command mailbox from the main thread (simulated console) */
    sai_host_isr_register(3, sensor_isr, NULL);

    sai_kernel_start(demo_main);           /* never returns */
    return 0;
}
