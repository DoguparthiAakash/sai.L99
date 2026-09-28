/*
 * ports/micropython/modsai.c
 * The `sai` MicroPython module: kernel and device services for scripts.
 *
 * Registered via MP_REGISTER_MODULE so the module name goes through the
 * normal qstr pipeline.  All calls map onto the portable sai.L99 public API;
 * scripts can sleep, spawn threads, signal events, drive devices and check
 * system state directly from Python.
 */
#include "py/runtime.h"
#include "py/mpstate.h"

#include "sai/kernel.h"
#include "sai/time.h"
#include "sai/device.h"
#include "sai/version.h"

#include <string.h>

#include "mphalport.h"

/* ------------------------------------------------------------------ */
/* time                                                                */
/* ------------------------------------------------------------------ */
static mp_obj_t mod_sai_sleep_ms(mp_obj_t ms_obj)
{
    mp_int_t ms = mp_obj_get_int(ms_obj);
    if (ms > 0) {
        sai_sleep((uint32_t)ms);
    } else {
        sai_yield();
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_sai_sleep_ms_obj, mod_sai_sleep_ms);

static mp_obj_t mod_sai_ticks_ms(void)
{
    return MP_OBJ_NEW_SMALL_INT((mp_int_t)sai_tick_count());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_sai_ticks_ms_obj, mod_sai_ticks_ms);

static mp_obj_t mod_sai_uptime(void)
{
    return MP_OBJ_NEW_SMALL_INT((mp_int_t)(sai_tick_count() / SAI_MS_TO_TICKS(1000)));
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_sai_uptime_obj, mod_sai_uptime);

/* ------------------------------------------------------------------ */
/* NOTE on threads: the VM is built with MICROPY_MULTIPLE_THREADS=0, so
 * Python code must only run on the single VM thread that hosts it.  Kernel
 * threads remain a C-level service (see the scripting service in
 * kernel/services/script.c); a future MICROPY_MULTIPLE_THREADS=1 build
 * could re-expose sai_thread_start() to Python.
 *
 * ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* system info                                                         */
/* ------------------------------------------------------------------ */
static mp_obj_t mod_sai_version(void)
{
    return mp_obj_new_str(SAI_VERSION_STRING, strlen(SAI_VERSION_STRING));
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_sai_version_obj, mod_sai_version);

static mp_obj_t mod_sai_ctx_switches(void)
{
    return MP_OBJ_NEW_SMALL_INT((mp_int_t)sai_ctx_switches());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_sai_ctx_switches_obj, mod_sai_ctx_switches);

/* ------------------------------------------------------------------ */
/* module table                                                        */
/* ------------------------------------------------------------------ */
static const mp_rom_map_elem_t sai_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_sai) },
    { MP_ROM_QSTR(MP_QSTR_sleep_ms),   MP_ROM_PTR(&mod_sai_sleep_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks_ms),   MP_ROM_PTR(&mod_sai_ticks_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_uptime),     MP_ROM_PTR(&mod_sai_uptime_obj) },
    { MP_ROM_QSTR(MP_QSTR_version),    MP_ROM_PTR(&mod_sai_version_obj) },
    { MP_ROM_QSTR(MP_QSTR_ctx_switches), MP_ROM_PTR(&mod_sai_ctx_switches_obj) },
};
static MP_DEFINE_CONST_DICT(sai_module_globals, sai_module_globals_table);

const mp_obj_module_t sai_user_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&sai_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_sai, sai_user_module);
