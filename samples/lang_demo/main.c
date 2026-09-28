/**
 * @file samples/lang_demo/main.c
 * @brief Multi-language demo: MicroPython, C, Rust payloads + services.
 *
 * Runs a MicroPython script, a compiled-C snippet and a Rust snippet via
 * the script service, demonstrates the events pub/sub and softrpc
 * registries, then starts the shell (type 'help' + Enter).
 */
#include <sai/kernel.h>
#include <sai/services.h>
#include <sai/console.h>
#include <string.h>
#include <stdio.h>

static int rpc_mul(uint32_t a0, uint32_t a1, int32_t *ret)
{
    if (ret != NULL) {
        *ret = (int32_t)(a0 * a1);
    }
    return SAI_OK;
}

/* Shell line source: host console stdin, one line per Enter. */
static int console_line(char *buf, uint32_t cap, void *user)
{
    (void)user;
    int c;
    uint32_t n = 0;
    while ((c = getchar()) != EOF && c != '\n') {
        if (n + 1u < cap) {
            buf[n++] = (char)c;
        }
    }
    if (c == EOF && n == 0u) {
        return -1;
    }
    buf[n] = '\0';
    return (int)n;
}

void lang_demo_main(void)
{
    sai_printf("\r\n=== sai.L99 multi-language demo ===\r\n");

    /* --- softrpc registry ------------------------------------------- */
    static const sai_softrpc_method_t mul = {
        .name = "mul", .id = 7, .handler = rpc_mul,
    };
    (void)sai_softrpc_register(&mul);
    int32_t prod = 0;
    (void)sai_softrpc_call("mul", 21, 2, &prod);
    sai_printf("[rpc]  mul(21, 2) = %ld\r\n", (long)prod);

    /* --- events pub/sub --------------------------------------------- */
    sai_event_t *ev = NULL;
    (void)sai_events_open("demo", &ev);
    (void)sai_events_publish("demo", 0x1u);
    uint32_t got = 0;
    (void)sai_events_wait("demo", 0x1u, SAI_EVENT_WAIT_ANY | SAI_EVENT_CONSUME,
                          &got, 100);
    sai_printf("[events] demo received 0x%lx\r\n", (unsigned long)got);

    /* --- MicroPython ------------------------------------------------- */
    static const char mp_code[] =
        "import sai\r\n"
        "print('hello from micropython on sai', sai.version())\r\n"
        "print('ticks:', sai.ticks_ms())\r\n";
    sai_script_result_t res;
    static char outbuf[512];
    memset(&res, 0, sizeof(res));
    res.output = outbuf;
    res.output_cap = sizeof(outbuf);

    sai_status_t rc = sai_script_run(SAI_LANG_MICROPYTHON, mp_code,
                                     sizeof(mp_code) - 1u, &res, 0);
    sai_printf("[mp]   rc=%d output:\r\n%s", (int)rc,
               (res.output_len > 0) ? res.output : "(none)\r\n");

    /* --- compiled C snippet (host toolchain) -------------------------- */
    (void)sai_lang_native_register("hello_c", SAI_LANG_C,
        "#include <stdio.h>\n"
        "int main(void){ printf(\"hello from C\\n\"); return 0; }\n",
        86);
    rc = sai_script_run(SAI_LANG_C, "hello_c", 7, NULL, 0);
    sai_printf("[c]    rc=%d\r\n", (int)rc);

    /* --- Rust snippet (host toolchain) -------------------------------- */
    (void)sai_lang_native_register("hello_rs", SAI_LANG_RUST,
        "fn main() { println!(\"hello from Rust\"); }\n",
        44);
    rc = sai_script_run(SAI_LANG_RUST, "hello_rs", 8, NULL, 0);
    sai_printf("[rust] rc=%d\r\n", (int)rc);

    /* --- services + interactive shell --------------------------------- */
    (void)sai_script_service_start();
    (void)sai_stats_service_start(2000u);
    (void)sai_shell_start(console_line, NULL, 12);
    sai_printf("[svc]  scriptd + statsd + sh running; type 'help'\r\n");
    sai_printf("=====================================\r\n");

    /* Kernel main thread stays resident; services keep running. */
    for (;;) {
        sai_sleep(1000);
    }
}

/* Host entry: boot the kernel; lang_demo_main is the kernel main thread. */
int main(void)
{
    if (sai_kernel_init() != SAI_OK) {
        return 2;
    }
    sai_kernel_start(lang_demo_main);   /* never returns */
    return 3;
}
