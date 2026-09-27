/**
 * @file samples/blinky/main_target.c
 * @brief sai.L99 blinky (ARM Cortex-M target): real LED tasks on the
 *        STM32F407-Discovery, shell-lite over USART2.
 *
 * main() runs on MSP after Reset_Handler; sai_kernel_start() enters
 * thread mode and never returns.
 */
#include <sai/kernel.h>
#include <sai/sync.h>
#include <sai/ipc.h>
#include <sai/time.h>
#include <sai/mem.h>
#include <sai/log.h>
#include <sai/device.h>
#include <sai/console.h>
#include <string.h>

extern void sai_board_led_set(uint32_t idx, bool on);
extern sai_status_t sai_board_console_setup(void);

static sai_mbox_t console_in;
static sai_thread_t t1, t2, t3, tshell;

static void led_task(void *arg)
{
    uint32_t idx = (uint32_t)(uintptr_t)arg;
    const uint32_t period = (idx + 1u) * 100u;
    bool on = false;
    for (;;) {
        on = !on;
        sai_board_led_set(idx, on);
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
            if (len > 0 && strcmp(line, "help") == 0) {
                sai_printf("sai.L99: led0/led1/led2 stats uptime\n");
            } else if (len > 0 && strcmp(line, "uptime") == 0) {
                sai_printf("uptime: %u ms\n", (unsigned)sai_uptime_ms());
            } else if (len > 0 && strcmp(line, "stats") == 0) {
                sai_printf("ctx=%u isr=%u\n",
                           (unsigned)sai_ctx_switches(),
                           (unsigned)sai_isr_reschedules());
            }
            len = 0;
        } else if (len + 1u < sizeof(line)) {
            line[len++] = (char)c;
        }
    }
}

/* Kernel main thread: set up the app, then park (other threads run). */
static void blinky_main(void)
{
    sai_printf("\nsai.L99 %s blinky (cortex-m4)\n", SAI_VERSION_STRING);

    (void)sai_mbox_init(&console_in, "conin", NULL, 32);

    (void)sai_thread_create(&t1, "led0", led_task, (void *)(uintptr_t)0, 20u, NULL, 1024, 0);
    (void)sai_thread_create(&t2, "led1", led_task, (void *)(uintptr_t)1, 20u, NULL, 1024, 0);
    (void)sai_thread_create(&t3, "led2", led_task, (void *)(uintptr_t)2, 20u, NULL, 1024, 0);
    (void)sai_thread_create(&tshell, "shell", shell_task, NULL, 12u, NULL, 2048, 0);

    (void)sai_thread_start(&t1);
    (void)sai_thread_start(&t2);
    (void)sai_thread_start(&t3);
    (void)sai_thread_start(&tshell);

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

    sai_kernel_start(blinky_main);     /* never returns */
    return 0;
}
