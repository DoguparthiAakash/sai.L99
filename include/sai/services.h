/**
 * @file sai/services.h
 * @brief User-space style services layered on the sai kernel.
 *
 * Services are named endpoints that run in their own (static) threads and
 * talk through message queues.  This header groups the four services that
 * ship with the RTOS plus the language-runtime registry they build on:
 *
 *  - script service  : run MicroPython / C / C++ / asm / Rust payloads
 *  - stats service   : kernel diagnostics snapshots
 *  - events service  : named pub/sub channels over event flags
 *  - softrpc service : name -> (method id, handler) dispatch registry
 *
 * All APIs return sai_status_t; SAI_ERR_NOENT means "no such service/name".
 */
#ifndef SAI_SERVICES_H
#define SAI_SERVICES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sai/types.h>
#include <sai/kernel.h>
#include <sai/ipc.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/* Service framework                                                   */
/* ================================================================== */

/** Opaque service endpoint (see kernel/services/service.c). */
typedef struct sai_service sai_service_t;

/** Service main loop prototype: runs until sai_service_stop(). */
typedef void (*sai_service_fn_t)(sai_service_t *svc);

/**
 * Register and start a service.
 *
 * @param name      Static service name ("scriptd", "statsd", ...).
 * @param main_fn   Service main loop.
 * @param stack     Stack buffer, or NULL to allocate from the kernel heap.
 * @param stack_size Stack size in bytes (0 = kernel default).
 * @param prio      Thread priority (0..30).
 * @return SAI_OK, SAI_ERR_NOENT (name taken), or a negative status.
 */
sai_status_t sai_service_start(const char *name, sai_service_fn_t main_fn,
                               void *stack, size_t stack_size, uint8_t prio);

/** Look up a service by name. */
sai_service_t *sai_service_find(const char *name);

/** Post a 4-byte message to a service (SAI_NO_WAIT semantics by default). */
sai_status_t sai_service_post(sai_service_t *svc, uint32_t msg);
sai_status_t sai_service_post_named(const char *name, uint32_t msg);

/** Ask a service to exit (synchronous: waits for the thread to finish). */
sai_status_t sai_service_stop(sai_service_t *svc);

/** True after sai_service_stop() completed for @p svc. */
bool sai_service_is_stopped(const sai_service_t *svc);

/** Service name / thread handle (diagnostics). */
const char *sai_service_name(const sai_service_t *svc);
sai_thread_t *sai_service_thread(sai_service_t *svc);

/**
 * Blocking receive helper for service loops: dequeue one message.
 * @return SAI_OK, SAI_ERR_TIMEOUT, or SAI_ERR_STATE (stop was requested).
 */
sai_status_t sai_service_recv(sai_service_t *svc, uint32_t *msg, int32_t timeout_ms);

/** Magic "stop" token delivered by sai_service_stop(). */
#define SAI_SERVICE_MSG_STOP 0x53544F50u  /* 'STOP' */

/* ================================================================== */
/* Language runtimes                                                   */
/* ================================================================== */

/** Payload languages supported by the script service. */
typedef enum {
    SAI_LANG_MICROPYTHON = 0,
    SAI_LANG_C,
    SAI_LANG_CPP,
    SAI_LANG_ASM,
    SAI_LANG_RUST,
    SAI_LANG_COUNT,
} sai_lang_t;

/** Execution result of a script payload. */
typedef struct {
    int32_t  exit_code;     /**< Backend-specific completion code.        */
    uint32_t duration_ms;   /**< Wall time of the execution.              */
    uint32_t output_cap;    /**< Caller: capacity of @c output.           */
    uint32_t output_len;    /**< Filled: bytes written to @c output.      */
    char    *output;        /**< Caller buffer for captured stdout.       */
} sai_script_result_t;

/**
 * Language backend: executes a payload in the calling thread's context.
 * Implementations must be self-contained (MicroPython runs in whichever
 * thread calls sai_script_run unless the VM is busy).
 */
typedef struct sai_lang_backend {
    const char *name;                       /**< "micropython", "c", ...   */
    sai_lang_t  lang;
    sai_status_t (*run)(const char *src, size_t len, void *arg);
    void       *arg;                        /**< Backend private.          */
    struct sai_lang_backend *next;
} sai_lang_backend_t;

/** Register a language backend (duplicates by name/lang are rejected). */
sai_status_t sai_lang_register(sai_lang_backend_t *backend);

/** Find a backend by language id. */
sai_lang_backend_t *sai_lang_get(sai_lang_t lang);

/** Find a backend by name ("micropython", "c", ...). */
sai_lang_backend_t *sai_lang_get_by_name(const char *name);

/** Map a language string to its id; SAI_LANG_MICROPYTHON if unknown. */
sai_lang_t sai_lang_from_name(const char *name);

/** Canonical name of a language ("micropython", "c", "cpp", "asm", "rust"). */
const char *sai_lang_name(sai_lang_t lang);

/* ================================================================== */
/* Script service                                                      */
/* ================================================================== */

/** Max payload length accepted by sai_script_run (static buffers). */
#ifndef CONFIG_SAI_SCRIPT_MAX_LEN
#define CONFIG_SAI_SCRIPT_MAX_LEN 4096
#endif

/** Synchronous script execution from the calling thread. */
sai_status_t sai_script_run(sai_lang_t lang, const char *src, size_t len,
                            sai_script_result_t *result, uint32_t timeout_ms);

/** Asynchronous run: posts to scriptd; poll sai_script_status(). */
sai_status_t sai_script_run_async(sai_lang_t lang, const char *src, size_t len);

/** Latest completed async run (0 = none finished yet). */
uint32_t sai_script_status(sai_lang_t *lang_out, int32_t *exit_code);

/** Completion counters (all runs, sync + async). */
uint32_t sai_script_runs(void);
uint32_t sai_script_errors(void);

/** Start/stop the script daemon ("scriptd"). */
sai_status_t sai_script_service_start(void);
sai_status_t sai_script_service_stop(void);

/** Output capture: hook called with chunks of script output (NULL = console). */
typedef void (*sai_script_out_fn_t)(const char *buf, size_t len, void *user);
void sai_script_set_output_hook(sai_script_out_fn_t fn, void *user);

/* ================================================================== */
/* C/C++/asm/Rust payload backends (portable no-ops on freestanding)   */
/* ================================================================== */

/**
 * Compile-and-run backends for the compiled languages.  On the host build
 * these use the platform toolchain (clang/gcc) via a service subprocess;
 * on the target they run a fixed builtin demo (no compiler on-target) or
 * return SAI_ERR_NOT_SUPPORTED when no payload is baked in.
 */
sai_status_t sai_lang_native_init(void);

/** Register an inline-compiled C/asm/Rust snippet (host builds only). */
sai_status_t sai_lang_native_register(const char *name, sai_lang_t lang,
                                      const char *source, size_t len);

/** Run a previously registered snippet by name. */
sai_status_t sai_lang_native_run(const char *name);

/* ================================================================== */
/* Stats service                                                       */
/* ================================================================== */

typedef struct {
    uint32_t ticks;             /**< sai_tick_count()                     */
    uint32_t ctx_switches;      /**< Total context switches               */
    uint32_t isr_reschedules;   /**< ISR-triggered reschedules            */
    uint32_t threads;           /**< Live thread count                    */
    uint32_t kobjects;          /**< Registered kernel objects            */
    uint32_t devices;           /**< Registered devices                   */
    uint32_t timers;            /**< Active timers                        */
    uint32_t heap_free;         /**< Kernel heap free bytes               */
    uint32_t heap_max_free;     /**< Largest contiguous free block        */
    uint32_t scripts_run;       /**< Script service: completed runs       */
    uint32_t script_errors;     /**< Script service: failed runs          */
    uint32_t budget_violations; /**< Budget enforcer: overruns (all tids) */
} sai_stats_snapshot_t;

/** Fill @p out with a coherent diagnostics snapshot (any thread). */
void sai_stats_get(sai_stats_snapshot_t *out);

/** Pretty-print a snapshot to the console (shell "stats" command). */
void sai_stats_print(const sai_stats_snapshot_t *s);

/** Start/stop the stats daemon (periodic SAI_LOGI heartbeat). */
sai_status_t sai_stats_service_start(uint32_t interval_ms);
sai_status_t sai_stats_service_stop(void);

/* ================================================================== */
/* Events service (named pub/sub channels)                             */
/* ================================================================== */

#define SAI_EVENTS_MAX_CHANNELS 8
#define SAI_EVENTS_NAME_MAX 12

/**
 * Create (or get) a named event channel.  Channels are statically pooled;
 * duplicate creation returns the existing channel with SAI_OK.
 */
sai_status_t sai_events_open(const char *name, sai_event_t **out);

/** Publish bits to a channel (ISR-safe; never blocks). */
sai_status_t sai_events_publish(const char *name, uint32_t flags);

/** Subscribe: wait for @p flags on @p name (see sai_event_wait opts). */
sai_status_t sai_events_wait(const char *name, uint32_t flags, uint32_t opts,
                             uint32_t *set_flags, int32_t timeout_ms);

/** Tear down all channels (tests). */
void sai_events_reset(void);

/* ================================================================== */
/* Shell service                                                       */
/* ================================================================== */

/**
 * Line source: fill @p buf with one command line (NUL-terminated), return
 * its length (0/ negative = no line available yet).  Boards wire this to
 * the UART RX path; tests feed scripted lines.
 */
typedef int (*sai_shell_line_fn_t)(char *buf, uint32_t cap, void *user);

/** Start the shell service ("sh"). */
sai_status_t sai_shell_start(sai_shell_line_fn_t line_source, void *user,
                             uint8_t prio);

/** Stop the shell service. */
sai_status_t sai_shell_stop(void);

/* ================================================================== */
/* SoftRPC: name -> method dispatch registry                           */
/* ================================================================== */

#define SAI_SOFTRPC_MAX_METHODS 16
#define SAI_SOFTRPC_NAME_MAX 12

typedef struct {
    const char *name;                  /**< Static method name.            */
    uint8_t     id;                    /**< Caller-assigned method id.     */
    sai_status_t (*handler)(uint32_t arg0, uint32_t arg1, int32_t *ret);
} sai_softrpc_method_t;

/** Register a method (duplicate names rejected with SAI_ERR_EXISTS). */
sai_status_t sai_softrpc_register(const sai_softrpc_method_t *method);

/** Look up a method by name. */
const sai_softrpc_method_t *sai_softrpc_find(const char *name);

/** Invoke by name: SAI_ERR_NOENT if unregistered, else the handler's rc. */
sai_status_t sai_softrpc_call(const char *name,
                              uint32_t arg0, uint32_t arg1, int32_t *ret);

/** Number of registered methods (diagnostics). */
uint32_t sai_softrpc_count(void);

#ifdef __cplusplus
}
#endif

#endif /* SAI_SERVICES_H */
