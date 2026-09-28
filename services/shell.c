/**
 * @file services/shell.c
 * @brief Shell service: line console with kernel/service commands.
 *
 * Commands: help, ps, stats, dev, ev, rpc, run, exec, mp, kill
 *
 * Input is a line source callback (board UART, test harness, ...); output
 * goes through the sai console.  The shell runs as a named service ("sh")
 * so sai_service_stop() reclaims it.
 */
#include <sai/services.h>
#include <sai/console.h>
#include <sai/devices2.h>
#include <sai/log.h>
#include <string.h>

#define SH_LINE_MAX 192

typedef struct shell_ctx {
    sai_service_t *svc;
    int (*line_source)(char *buf, uint32_t cap, void *user);
    void *user;
} shell_ctx_t;

static shell_ctx_t s_sh;

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static void sh_puts(const char *s)
{
    sai_console_write(s);
}

static int split_argv(char *line, char **argv, int max_argv)
{
    int argc = 0;
    char *p = line;
    while (*p != '\0' && argc < max_argv) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        argv[argc++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
        }
        if (*p != '\0') {
            *p++ = '\0';
        }
    }
    return argc;
}

/** Freestanding-safe number parser: 0x hex or decimal. */
static uint32_t sh_atou(const char *s)
{
    uint32_t v = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        while (*s != '\0') {
            char c = *s;
            uint32_t d;
            if (c >= '0' && c <= '9')      d = (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
            else break;
            v = v * 16u + d;
            s++;
        }
    } else {
        while (*s >= '0' && *s <= '9') {
            v = v * 10u + (uint32_t)(*s - '0');
            s++;
        }
    }
    return v;
}

/* ------------------------------------------------------------------ */
/* commands                                                            */
/* ------------------------------------------------------------------ */

static void cmd_help(void)
{
    sh_puts("commands: help ps stats dev ev rpc run exec mp kill\r\n"
            "  run <lang> <src>   run a payload (micropython|c|cpp|asm|rust)\r\n"
            "  exec <name>        run a registered native snippet\r\n"
            "  mp <code...>       shortcut for `run micropython <code>`\r\n");
}

static void cmd_ps(void)
{
    sai_stats_snapshot_t s;
    sai_stats_get(&s);
    sai_printf("threads=%lu kobj=%lu devices=%lu ctx=%lu\r\n",
               (unsigned long)s.threads, (unsigned long)s.kobjects,
               (unsigned long)s.devices, (unsigned long)s.ctx_switches);
}

static void cmd_stats(void)
{
    sai_stats_snapshot_t s;
    sai_stats_get(&s);
    sai_stats_print(&s);
}

static void cmd_dev(void)
{
    sai_device_t *it = NULL;
    while ((it = sai_device_iter(it)) != NULL) {
        sai_printf("dev %s type=%u\r\n", it->name, (unsigned)it->type);
    }
}

static void cmd_ev(int argc, char **argv)
{
    if (argc < 2) {
        sh_puts("usage: ev <name> <flags> [wait]\r\n");
        return;
    }
    uint32_t flags = (argc > 2) ? sh_atou(argv[2]) : 0x1u;
    if (argc > 3) {
        uint32_t got = 0;
        sai_status_t rc = sai_events_wait(argv[1], flags,
                                          SAI_EVENT_WAIT_ANY, &got, 1000);
        sai_printf("ev %s wait rc=%d got=0x%lx\r\n",
                   argv[1], (int)rc, (unsigned long)got);
    } else {
        sai_status_t rc = sai_events_publish(argv[1], flags);
        sai_printf("ev %s pub rc=%d\r\n", argv[1], (int)rc);
    }
}

static int rpc_add(uint32_t a0, uint32_t a1, int32_t *ret)
{
    if (ret != NULL) {
        *ret = (int32_t)(a0 + a1);
    }
    return SAI_OK;
}

static void cmd_rpc(int argc, char **argv)
{
    static const sai_softrpc_method_t add = {
        .name = "add", .id = 1, .handler = rpc_add,
    };
    (void)sai_softrpc_register(&add);   /* idempotent-ish: dup -> NOENT */
    if (argc < 2) {
        sh_puts("usage: rpc <method> [a b]\r\n");
        return;
    }
    int32_t ret = 0;
    sai_status_t rc = sai_softrpc_call(argv[1],
                                       (argc > 2) ? sh_atou(argv[2]) : 0u,
                                       (argc > 3) ? sh_atou(argv[3]) : 0u,
                                       &ret);
    sai_printf("rpc %s rc=%d ret=%ld\r\n", argv[1], (int)rc, (long)ret);
}

static void cmd_run(int argc, char **argv)
{
    if (argc < 3) {
        sh_puts("usage: run <lang> <source...>\r\n");
        return;
    }
    sai_lang_t lang = sai_lang_from_name(argv[1]);
    /* Rejoin the source words. */
    static char src[CONFIG_SAI_SCRIPT_MAX_LEN];
    size_t off = 0;
    for (int i = 2; i < argc && off + 2 < sizeof(src); i++) {
        size_t len = strlen(argv[i]);
        if (off + len + 2u >= sizeof(src)) {
            break;
        }
        memcpy(&src[off], argv[i], len);
        off += len;
        src[off++] = ' ';
    }
    if (off > 0) {
        src[--off] = '\0';
    }

    sai_script_result_t res;
    static char outbuf[512];
    memset(&res, 0, sizeof(res));
    res.output = outbuf;
    res.output_cap = sizeof(outbuf);

    sai_printf("run %s ...\r\n", sai_lang_name(lang));
    sai_status_t rc = sai_script_run(lang, src, off, &res, 0);
    if (res.output_len > 0) {
        sai_console_write(res.output);
        sh_puts("\r\n");
    }
    sai_printf("run rc=%d (%lu ms)\r\n",
               (int)rc, (unsigned long)res.duration_ms);
}

static void cmd_exec(int argc, char **argv)
{
    if (argc < 2) {
        sh_puts("usage: exec <snippet-name>\r\n");
        return;
    }
    sai_status_t rc = sai_lang_native_run(argv[1]);
    sai_printf("exec %s rc=%d\r\n", argv[1], (int)rc);
}

static void cmd_mp(int argc, char **argv)
{
    if (argc < 2) {
        sh_puts("usage: mp <code...>\r\n");
        return;
    }
    char *mp_argv[2] = { "mp", NULL };
    static char joined[CONFIG_SAI_SCRIPT_MAX_LEN];
    size_t off = 0;
    for (int i = 1; i < argc && off + 2 < sizeof(joined); i++) {
        size_t len = strlen(argv[i]);
        if (off + len + 2u >= sizeof(joined)) {
            break;
        }
        memcpy(&joined[off], argv[i], len);
        off += len;
        joined[off++] = ' ';
    }
    if (off > 0) {
        joined[--off] = '\0';
    }
    mp_argv[1] = joined;
    cmd_run(2, mp_argv);
}

static void cmd_kill(int argc, char **argv)
{
    if (argc < 2) {
        sh_puts("usage: kill <service>\r\n");
        return;
    }
    sai_service_t *svc = sai_service_find(argv[1]);
    if (svc == NULL) {
        sai_printf("kill: no service '%s'\r\n", argv[1]);
        return;
    }
    sai_printf("kill %s rc=%d\r\n", argv[1], (int)sai_service_stop(svc));
}

/* ------------------------------------------------------------------ */
/* main loop                                                           */
/* ------------------------------------------------------------------ */

static void shell_loop(sai_service_t *svc)
{
    static char line[SH_LINE_MAX];
    s_sh.svc = svc;

    sh_puts("sai shell. type 'help'\r\n");
    for (;;) {
        int n = s_sh.line_source(line, sizeof(line), s_sh.user);
        if (n <= 0) {
            sai_sleep(10);
            continue;
        }
        char *argv[8];
        int argc = split_argv(line, argv, 8);
        if (argc == 0) {
            continue;
        }
        if (strcmp(argv[0], "help") == 0)      { cmd_help(); }
        else if (strcmp(argv[0], "ps") == 0)   { cmd_ps(); }
        else if (strcmp(argv[0], "stats") == 0){ cmd_stats(); }
        else if (strcmp(argv[0], "dev") == 0)  { cmd_dev(); }
        else if (strcmp(argv[0], "ev") == 0)   { cmd_ev(argc, argv); }
        else if (strcmp(argv[0], "rpc") == 0)  { cmd_rpc(argc, argv); }
        else if (strcmp(argv[0], "run") == 0)  { cmd_run(argc, argv); }
        else if (strcmp(argv[0], "exec") == 0) { cmd_exec(argc, argv); }
        else if (strcmp(argv[0], "mp") == 0)   { cmd_mp(argc, argv); }
        else if (strcmp(argv[0], "kill") == 0) { cmd_kill(argc, argv); }
        else {
            sai_printf("shell: unknown '%s' (try help)\r\n", argv[0]);
        }
    }
}

sai_status_t sai_shell_start(int (*line_source)(char *buf, uint32_t cap, void *user),
                             void *user, uint8_t prio)
{
    if (line_source == NULL) {
        return SAI_ERR_INVAL;
    }
    if (sai_service_find("sh") != NULL) {
        return SAI_ERR_NOENT;
    }
    s_sh.line_source = line_source;
    s_sh.user = user;
    return sai_service_start("sh", shell_loop, NULL, 4096, prio);
}

sai_status_t sai_shell_stop(void)
{
    sai_service_t *svc = sai_service_find("sh");
    if (svc == NULL) {
        return SAI_ERR_NOINIT;
    }
    return sai_service_stop(svc);
}
