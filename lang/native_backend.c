/**
 * @file lang/native_backend.c
 * @brief C / C++ / asm / Rust payload backends.
 *
 * Freestanding targets have no on-chip compiler, so "native" payloads are
 * supported two ways:
 *
 *  - host builds: snippets registered with sai_lang_native_register() are
 *    written to a scratch file, compiled with the platform toolchain
 *    (clang / clang++ / rustc) and executed as a child process.  The child
 *    prints to the real console; its exit code becomes the run result.
 *  - target builds: sai_lang_native_run() returns SAI_ERR_NOTSUP unless a
 *    payload was pre-registered and linked in (builtin demo mode), in which
 *    case the registered handler runs.
 *
 * No dynamic memory: the snippet registry is a static pool.
 */
#include <sai/services.h>
#include <sai/console.h>
#include <sai/log.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAI_NATIVE_MAX_SNIPPETS 4
#define SAI_NATIVE_SRC_MAX      1024
#define SAI_NATIVE_NAME_MAX     16

typedef struct {
    char     name[SAI_NATIVE_NAME_MAX];
    sai_lang_t lang;
    char     src[SAI_NATIVE_SRC_MAX];
    size_t   len;
    bool     used;
    /* Optional pre-built handler (target/builtin mode). */
    sai_status_t (*builtin)(void);
} native_snippet_t;

static native_snippet_t s_snippets[SAI_NATIVE_MAX_SNIPPETS];

static sai_status_t native_run_by_name(const char *src, size_t len, void *arg);

#ifdef SAI_HOST_BUILD

typedef struct {
    const char *compiler;   /* toolchain front end                   */
    const char *ext;        /* scratch file extension                */
    const char *extra;      /* extra compiler flags                  */
} native_tool_t;

static const native_tool_t *tool_for(sai_lang_t lang)
{
    static const native_tool_t tools[] = {
        [SAI_LANG_C]   = { "clang",   ".c",   "" },
        [SAI_LANG_CPP] = { "clang++", ".cpp", "" },
        [SAI_LANG_ASM] = { "clang",   ".S",   "" },
        [SAI_LANG_RUST] = { "rustc",  ".rs",  "" },
    };
    if ((int)lang < 0 || lang >= SAI_LANG_COUNT ||
        lang == SAI_LANG_MICROPYTHON) {
        return NULL;
    }
    return &tools[lang];
}

/** Run @p cmd via the shell; returns true when the tool exited 0. */
static bool native_system(const char *cmd)
{
    int rc = system(cmd);
    return rc == 0;
}

static sai_status_t native_compile_and_run(const native_snippet_t *snip)
{
    const native_tool_t *tool = tool_for(snip->lang);
    if (tool == NULL) {
        return SAI_ERR_NOTSUP;
    }
    const char *tmp = getenv("TEMP");
    if (tmp == NULL) {
        tmp = ".";
    }
    char path[128];
    char out[128];
    char cmd[512];
    sai_snprintf(path, sizeof(path), "%s\\sai_snip_%s%s", tmp, snip->name, tool->ext);
    sai_snprintf(out, sizeof(out), "%s\\sai_snip_%s.exe", tmp, snip->name);

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return SAI_ERR_IO;
    }
    if (fwrite(snip->src, 1, snip->len, f) != snip->len) {
        (void)fclose(f);
        return SAI_ERR_IO;
    }
    (void)fclose(f);

    /* Compile (output to the real console so build errors are visible). */
    sai_snprintf(cmd, sizeof(cmd), "%s %s \"%s\" -o \"%s\" 2>&1",
                 tool->compiler, tool->extra, path, out);
    if (!native_system(cmd)) {
        sai_printf("script: native '%s' failed to build\r\n", snip->name);
        return SAI_ERR_NOTSUP;
    }
    /* Execute; the child's exit code is the payload's result. */
    sai_snprintf(cmd, sizeof(cmd), "\"%s\"", out);
    int rc = system(cmd);
    return rc == 0 ? SAI_OK : SAI_ERR_STATE;
}

#else /* target builds */

static sai_status_t native_compile_and_run(const native_snippet_t *snip)
{
    if (snip->builtin != NULL) {
        return snip->builtin();
    }
    /* No compiler on-target: payload must be baked in at build time. */
    return SAI_ERR_NOTSUP;
}

#endif /* SAI_HOST_BUILD */

sai_status_t sai_lang_native_register(const char *name, sai_lang_t lang,
                                      const char *source, size_t len)
{
    if (name == NULL || source == NULL ||
        strlen(name) >= SAI_NATIVE_NAME_MAX ||
        lang == SAI_LANG_MICROPYTHON || lang >= SAI_LANG_COUNT) {
        return SAI_ERR_INVAL;
    }
    if (len > SAI_NATIVE_SRC_MAX) {
        return SAI_ERR_BOUNDS;
    }
    native_snippet_t *free_slot = NULL;
    for (uint32_t i = 0; i < SAI_NATIVE_MAX_SNIPPETS; i++) {
        native_snippet_t *s = &s_snippets[i];
        if (s->used && strcmp(s->name, name) == 0) {
            return SAI_ERR_NOENT;          /* name taken */
        }
        if (!s->used && free_slot == NULL) {
            free_slot = s;
        }
    }
    if (free_slot == NULL) {
        return SAI_ERR_FULL;
    }
    memset(free_slot, 0, sizeof(*free_slot));
    strncpy(free_slot->name, name, sizeof(free_slot->name) - 1u);
    free_slot->lang = lang;
    memcpy(free_slot->src, source, len);
    free_slot->len = len;
    free_slot->used = true;
    return SAI_OK;
}

sai_status_t sai_lang_native_run(const char *name)
{
    if (name == NULL) {
        return SAI_ERR_INVAL;
    }
    for (uint32_t i = 0; i < SAI_NATIVE_MAX_SNIPPETS; i++) {
        native_snippet_t *s = &s_snippets[i];
        if (s->used && strcmp(s->name, name) == 0) {
            return native_compile_and_run(s);
        }
    }
    return SAI_ERR_NOENT;
}

sai_status_t sai_lang_native_init(void)
{
    /* Register one backend per compiled language.  Payloads go through
     * sai_script_run(lang, name, ...) where `name` is a snippet registered
     * with sai_lang_native_register() (no on-target compiler, so scripts
     * reference pre-registered/pre-built units rather than raw source). */
    static sai_lang_backend_t backends[SAI_LANG_COUNT];
    static const struct { const char *name; sai_lang_t lang; } defs[] = {
        { "c",    SAI_LANG_C    },
        { "cpp",  SAI_LANG_CPP  },
        { "asm",  SAI_LANG_ASM  },
        { "rust", SAI_LANG_RUST },
    };
    for (uint32_t i = 0; i < sizeof(defs) / sizeof(defs[0]); i++) {
        backends[defs[i].lang].name = defs[i].name;
        backends[defs[i].lang].lang = defs[i].lang;
        backends[defs[i].lang].arg  = NULL;
        backends[defs[i].lang].run  = native_run_by_name;
        (void)sai_lang_register(&backends[defs[i].lang]);
    }
    return SAI_OK;
}

static sai_status_t native_run_by_name(const char *src, size_t len, void *arg)
{
    (void)len;
    (void)arg;
    return sai_lang_native_run(src);
}
