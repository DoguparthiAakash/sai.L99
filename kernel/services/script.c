/**
 * @file kernel/services/script.c
 * @brief Script service: runs MicroPython / C / C++ / asm / Rust payloads.
 *
 *  - sai_script_run()        : synchronous, executes in the calling thread
 *                              (scriptd not required).
 *  - sai_script_run_async()  : posts a request to the "scriptd" service,
 *                              which runs one payload at a time and records
 *                              the outcome for sai_script_status().
 *
 * Output capture: while a payload runs, the MicroPython stdout sink is
 * redirected into a static buffer so callers can retrieve the text.
 */
#include <sai/services.h>
#include <sai/console.h>
#include <sai/log.h>
#include <sai/time.h>
#include <string.h>

#define SAI_SCRIPT_OUT_MAX 1024

typedef struct {
    char     buf[SAI_SCRIPT_OUT_MAX];
    uint32_t len;
    bool     overflow;
} script_outbuf_t;

static script_outbuf_t s_outbuf;

static void script_out_capture(const char *buf, size_t len, void *user)
{
    (void)user;
    uint32_t room = (uint32_t)(SAI_SCRIPT_OUT_MAX - 1u - s_outbuf.len);
    if (len > room) {
        len = room;
        s_outbuf.overflow = true;
    }
    memcpy(&s_outbuf.buf[s_outbuf.len], buf, len);
    s_outbuf.len += (uint32_t)len;
    s_outbuf.buf[s_outbuf.len] = '\0';
}

/* ------------------------------------------------------------------ */
/* Async request protocol (scriptd message queue is 4 bytes wide)      */
/* ------------------------------------------------------------------ */
#define SCRIPT_REQ_MAGIC 0x53505231u      /* 'SPR1' */

/* Requests are posted as a pointer split across two messages; the result
 * descriptor is static so no dynamic allocation is involved. */
typedef struct {
    uint32_t     magic;
    sai_lang_t   lang;
    uint32_t     len;
    char         src[CONFIG_SAI_SCRIPT_MAX_LEN];
    volatile int32_t exit_code;         /* SAI_ERR_AGAIN while pending     */
} script_req_t;

static script_req_t s_req;
static sai_service_t *s_scriptd;

/* Latest completed async run (published by scriptd). */
static volatile uint32_t s_done_seq;
static volatile sai_lang_t s_done_lang;
static volatile int32_t    s_done_code;

static uint32_t s_scripts_run;
static uint32_t s_script_errors;

static void scriptd_loop(sai_service_t *svc)
{
    for (;;) {
        /* Requests arrive as two 4-byte messages: high then low half of the
         * request descriptor address (portable across 32/64-bit hosts). */
        uint32_t hi;
        sai_status_t rc = sai_service_recv(svc, &hi, SAI_WAIT_FOREVER);
        if (rc == SAI_ERR_STATE) {
            return;                     /* stop requested */
        }
        if (rc != SAI_OK) {
            continue;
        }
        uint32_t lo;
        if (sai_service_recv(svc, &lo, 100) != SAI_OK) {
            continue;                   /* malformed request, drop it */
        }
        uintptr_t addr = ((uintptr_t)hi << 32) | (uintptr_t)lo;
        script_req_t *req = (script_req_t *)addr;
        if (req->magic != SCRIPT_REQ_MAGIC) {
            continue;
        }

        sai_lang_backend_t *be = sai_lang_get(req->lang);
        rc = (be != NULL)
            ? be->run(req->src, req->len, be->arg)
            : SAI_ERR_NOENT;

        req->exit_code = (rc == SAI_OK) ? 0 : rc;
        s_done_lang = req->lang;
        s_done_code = req->exit_code;
        s_done_seq++;
        if (rc == SAI_OK) {
            s_scripts_run++;
        } else {
            s_script_errors++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

sai_status_t sai_script_run(sai_lang_t lang, const char *src, size_t len,
                            sai_script_result_t *result, uint32_t timeout_ms)
{
    if (src == NULL || lang >= SAI_LANG_COUNT ||
        (result != NULL && timeout_ms != 0u)) {
        return SAI_ERR_INVAL;
    }
    sai_lang_backend_t *be = sai_lang_get(lang);
    if (be == NULL) {
        return SAI_ERR_NOENT;
    }

    if (result != NULL) {
        /* Captured-output mode: buffer must be filled before the run. */
        s_outbuf.len = 0;
        s_outbuf.overflow = false;
        s_outbuf.buf[0] = '\0';
        sai_script_set_output_hook(script_out_capture, NULL);
    }

    uint32_t t0 = sai_tick_count();
    sai_status_t rc = be->run(src, len, be->arg);
    uint32_t dt = sai_tick_count() - t0;

    if (result != NULL) {
        sai_script_set_output_hook(NULL, NULL);
        result->exit_code = (rc == SAI_OK) ? 0 : rc;
        result->duration_ms = dt;
        if (result->output != NULL && result->output_cap > 0u) {
            uint32_t n = s_outbuf.len;
            if (n > result->output_cap - 1u) {
                n = result->output_cap - 1u;
            }
            memcpy(result->output, s_outbuf.buf, n);
            result->output[n] = '\0';
            result->output_len = n;
        } else {
            result->output_len = s_outbuf.len;
        }
    }
    if (rc == SAI_OK) {
        s_scripts_run++;
    } else {
        s_script_errors++;
    }
    return rc;
}

sai_status_t sai_script_run_async(sai_lang_t lang, const char *src, size_t len)
{
    if (src == NULL || len == 0u || len >= CONFIG_SAI_SCRIPT_MAX_LEN ||
        lang >= SAI_LANG_COUNT) {
        return SAI_ERR_INVAL;
    }
    if (s_scriptd == NULL) {
        sai_status_t rc = sai_script_service_start();
        if (rc != SAI_OK) {
            return rc;
        }
    }
    s_req.magic = SCRIPT_REQ_MAGIC;
    s_req.lang = lang;
    s_req.len = (uint32_t)len;
    memcpy(s_req.src, src, len);
    s_req.src[len] = '\0';
    s_req.exit_code = SAI_ERR_AGAIN;

    /* Hand the request to scriptd as two 4-byte address halves. */
    uintptr_t addr = (uintptr_t)&s_req;
    uint32_t hi = (uint32_t)((uintptr_t)addr >> 32);
    uint32_t lo = (uint32_t)(addr & 0xFFFFFFFFu);
    sai_status_t rc = sai_service_post(s_scriptd, hi);
    if (rc == SAI_OK) {
        rc = sai_service_post(s_scriptd, lo);
    }
    if (rc != SAI_OK) {
        s_req.magic = 0;
        return rc;
    }
    return SAI_OK;
}

uint32_t sai_script_status(sai_lang_t *lang_out, int32_t *exit_code)
{
    uint32_t seq = s_done_seq;
    if (lang_out != NULL) {
        *lang_out = s_done_lang;
    }
    if (exit_code != NULL) {
        *exit_code = s_done_code;
    }
    return seq;
}

uint32_t sai_script_runs(void)
{
    return s_scripts_run;
}

uint32_t sai_script_errors(void)
{
    return s_script_errors;
}

sai_status_t sai_script_service_start(void)
{
    if (s_scriptd != NULL && !sai_service_is_stopped(s_scriptd)) {
        return SAI_OK;                  /* already running */
    }
    sai_status_t rc = sai_service_start("scriptd", scriptd_loop, NULL,
                                        6144, 12);
    if (rc != SAI_OK) {
        return rc;
    }
    s_scriptd = sai_service_find("scriptd");
    return (s_scriptd != NULL) ? SAI_OK : SAI_ERR_NOENT;
}

sai_status_t sai_script_service_stop(void)
{
    if (s_scriptd == NULL) {
        return SAI_ERR_NOINIT;
    }
    sai_status_t rc = sai_service_stop(s_scriptd);
    s_scriptd = NULL;
    return rc;
}
