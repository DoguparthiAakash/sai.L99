/**
 * @file samples/blinky/main.c
 * @brief sai.L99 blinky + scheduler demo (host build).
 *
 * Three "LED" tasks at different periods print state changes, a shell-lite
 * processes simulated console input (help/stats/uptime), and a stats thread
 * reports scheduler counters. On target the LED tasks toggle real GPIO and
 * the shell reads the UART: same code path, different console backend.
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

static sai_mbox_t console_in;
static volatile uint32_t s_led_state[3];

static sai_thread_t t1, t2, t3, tshell;

static void led_task(void *arg)
{
    uint32_t idx = (uint32_t)(uintptr_t)arg;
    const uint32_t period = (idx + 1u) * 100u;
    for (;;) {
        s_led_state[idx] ^= 1u;
        sai_printf("[led%u] %s (tick %u)\n", (unsigned)idx,
                   s_led_state[idx] ? "ON " : "OFF", (unsigned)sai_tick_count());
        sai_sleep(period);
    }
}

static void shell_task(void *arg)
{
    (void)arg;
    char line[32];
    uint32_t len = 0;
    for (;;) {
        uint8_t c;
        if (sai_mbox_get(&console_in, &c, 100) != SAI_OK) {
            continue;
        }
        if (c == '\r' || c == '\n') {
            line[len] = '\0';
            if (len > 0) {
                if (strcmp(line, "help") == 0) {
                    sai_printf("commands: help, stats, uptime\n");
                } else if (strcmp(line, "stats") == 0) {
                    sai_printf("ctx=%u isr=%u heapfree=%u\n",
                               (unsigned)sai_ctx_switches(),
                               (unsigned)sai_isr_reschedules(),
                               (unsigned)sai_mem_free_bytes());
                } else if (strcmp(line, "uptime") == 0) {
                    sai_printf("uptime: %u ms (%u ticks)\n",
                               (unsigned)sai_uptime_ms(),
                               (unsigned)sai_tick_count());
                } else {
                    sai_printf("unknown: %s (try help)\n", line);
                }
            }
            len = 0;
        } else if (len + 1u < sizeof(line)) {
            line[len++] = (char)c;
        }
    }
}

/* Simulated console: type into stdin, one line per shell command. */
static void console_feeder(void *arg)
{
    (void)arg;
    int ch;
    while ((ch = getchar()) != EOF) {
        (void)sai_mbox_put(&console_in, (uint8_t)ch, SAI_WAIT_FOREVER);
        if (ch == '\n') {
            /* nothing: shell handles CR/LF */
        }
    }
}

/* Kernel main thread: park; the LED tasks own the demo. */
static void blinky_main(void)
{
    for (;;) {
        sai_sleep(1000);
    }
}

int main(void)
{
    printf("sai.L99 %s blinky demo (host build)\n", SAI_VERSION_STRING);
    printf("type 'help' + Enter for the shell-lite\n\n");

    if (sai_kernel_init() != SAI_OK) {
        return 1;
    }
    sai_host_atexit();

    sai_mbox_init(&console_in, "conin", NULL, 32);

    sai_thread_create(&t1, "led0", led_task, (void *)(uintptr_t)0, 20u, NULL, 4096, 0);
    sai_thread_create(&t2, "led1", led_task, (void *)(uintptr_t)1, 20u, NULL, 4096, 0);
    sai_thread_create(&t3, "led2", led_task, (void *)(uintptr_t)2, 20u, NULL, 4096, 0);
    sai_thread_create(&tshell, "shell", shell_task, NULL, 12u, NULL, 8192, 0);

    sai_thread_start(&t1);
    sai_thread_start(&t2);
    sai_thread_start(&t3);
    sai_thread_start(&tshell);

    static sai_thread_t feeder;
    sai_thread_create(&feeder, "feeder", console_feeder, NULL, 30u, NULL, 4096, 0);
    sai_thread_start(&feeder);

    sai_kernel_start(blinky_main);
    return 0;
}

