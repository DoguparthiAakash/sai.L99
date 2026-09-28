/**
 * @file lang/mp_backend.c
 * @brief MicroPython language backend: registers the vendored VM as a
 *        sai script language and executes payloads on demand.
 *
 * The VM is global (one heap, one interpreter state), so execution is
 * serialized with a mutex.  sai_mp_init() is called lazily on the first
 * run; sai_mp_exec() compiles + runs the payload in the calling thread.
 */
#include <sai/services.h>
#include <sai/sync.h>
#include <sai/log.h>

#include "py/mpconfig.h"
#include "py/nlr.h"
#include "py/compile.h"
#include "py/runtime.h"
#include "py/gc.h"

extern void sai_mp_init(void);
extern void sai_mp_deinit(void);
extern sai_status_t sai_mp_exec(const char *src, size_t len);

static sai_mutex_t s_mp_lock;
static bool        s_mp_lock_init;
static bool        s_mp_ready;

static sai_status_t mp_backend_run(const char *src, size_t len, void *arg)
{
    (void)arg;
    if (src == NULL) {
        return SAI_ERR_INVAL;
    }
    if (!s_mp_lock_init) {
        return SAI_ERR_NOINIT;
    }
    sai_status_t rc = sai_mutex_lock(&s_mp_lock);
    if (rc != SAI_OK) {
        return rc;
    }
    if (!s_mp_ready) {
        sai_mp_init();
        s_mp_ready = true;
    }
    rc = sai_mp_exec(src, len);
    if (rc != SAI_OK) {
        /* VM state may be dirty after an uncaught exception; re-init keeps
         * the next payload in a clean interpreter. */
        sai_mp_deinit();
        sai_mp_init();
    }
    (void)sai_mutex_unlock(&s_mp_lock);
    return rc;
}

static sai_lang_backend_t s_mp_backend = {
    .name = "micropython",
    .lang = SAI_LANG_MICROPYTHON,
    .run  = mp_backend_run,
    .arg  = NULL,
};

sai_status_t sai_lang_native_init(void);

void sai_script_service_mp_init(void)
{
    if (!s_mp_lock_init) {
        if (sai_mutex_init(&s_mp_lock, "mplock", false) != SAI_OK) {
            return;
        }
        s_mp_lock_init = true;
    }
    (void)sai_lang_register(&s_mp_backend);
    (void)sai_lang_native_init();
}
