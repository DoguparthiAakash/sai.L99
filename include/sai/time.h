/**
 * @file sai/time.h
 * @brief Kernel time: ticks, timeouts, one-shot and periodic timers.
 *
 * All kernel timers run off the tick (or the tickless compare). Callbacks
 * fire in tick/ISR context: keep them short and never block.
 */
#ifndef SAI_TIME_H
#define SAI_TIME_H

#include <sai/types.h>
#include <sai/config.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Milliseconds per kernel tick (from Kconfig; 1..1000). */
#define SAI_TICK_MS   CONFIG_SAI_TICK_MS
/** Ticks per second, derived. */
#define SAI_TICK_HZ   (1000u / (SAI_TICK_MS))

/** Ticks from milliseconds (rounds up; SAI_NO_WAIT/SAI_WAIT_FOREVER pass through). */
#define SAI_MS_TO_TICKS(ms)  ((uint32_t)(((uint32_t)(ms) + SAI_TICK_MS - 1u) / SAI_TICK_MS))
/** Ticks from microseconds (rounds up). */
#define SAI_US_TO_TICKS(us)  ((uint32_t)(((uint32_t)(us) + (SAI_TICK_MS * 1000u) - 1u) / (SAI_TICK_MS * 1000u)))

/** Timer callback. Runs in tick/ISR context — must not block. */
typedef void (*sai_timer_fn_t)(void *arg);

/**
 * Kernel timer object. Can be static or dynamic; initialize with
 * sai_timer_init() before use.
 */
typedef struct sai_timer {
    sai_kobj_t     kobj;
    sai_timer_fn_t fn;        /**< Callback.                              */
    void          *arg;       /**< Callback argument.                     */
    uint32_t       interval;  /**< Reload for periodic timers (ticks).    */
    uint32_t       deadline;  /**< Absolute expiry tick.                  */
    uint8_t        active;    /**< Running?                               */
    uint8_t        periodic;  /**< Auto-reload?                           */
    struct sai_timer *next;   /**< Internal timer list linkage.           */
} sai_timer_t;

/** Milliseconds since boot (wraps at 2^32 ms ≈ 49.7 days). */
uint32_t sai_uptime_ms(void);
/** Ticks since boot. */
uint32_t sai_tick_count(void);

/** Busy-wait at least @p us microseconds (early-boot / short delays). */
void sai_busy_sleep_us(uint32_t us);

/**
 * Initialize a timer. @p periodic selects auto-reload; @p interval_ms is the
 * period (periodic) or delay (one-shot).
 */
sai_status_t sai_timer_init(sai_timer_t *t, const char *name,
                            sai_timer_fn_t fn, void *arg,
                            uint32_t interval_ms, bool periodic);

/** Start (or restart) a timer. */
sai_status_t sai_timer_start(sai_timer_t *t);

/** Stop a timer. Safe on stopped timers. */
sai_status_t sai_timer_stop(sai_timer_t *t);

/** Change the interval of a running timer and restart it. */
sai_status_t sai_timer_restart(sai_timer_t *t, uint32_t interval_ms);

/** True if the timer is currently running. */
bool sai_timer_is_active(const sai_timer_t *t);

/** Remaining time until expiry in milliseconds (0 if expired/stopped). */
uint32_t sai_timer_remaining_ms(const sai_timer_t *t);

/** Number of currently active timers (diagnostics). */
uint32_t sai_timer_active_count(void);

/** Expire all timers whose deadline has passed (called from tick). */
void _sai_timers_tick(void);

/** Next absolute expiry tick among active timers (0 if none). */
uint32_t _sai_timers_next_deadline(void);

#ifdef __cplusplus
}
#endif

#endif /* SAI_TIME_H */
