# sai.L99 services and language runtimes

## Latency budget enforcer (`sai/budget.h`) — differentiator

Declare a per-thread CPU budget; the kernel enforces it on the scheduler
path and acts on overrun:

```c
/* max 50 ticks of CPU per 100-tick window; demote to prio 20 on overrun */
sai_budget_set(thread, 100, 50, SAI_BUDGET_ACTION_DEMOTE, 20);
```

Actions: `LOG` (record), `DEMOTE` (drop to a floor priority, auto-restore
on `sai_budget_clear`), `KILL` (terminate the offender). Accounting runs at
every context switch and tick — no blocking, no allocation. The clock is
injectable (`sai_budget_set_clock`) so enforcement is deterministically
testable on the host and reusable by the digital-twin mode.

## Tracing (`sai/trace.h`) — differentiator substrate

A 512-slot overwrite ring records context switches, ISR entry/exit and
budget violations with tick timestamps and monotonic sequence numbers:

```c
sai_trace_event_rec_t evs[32];
uint32_t n = sai_trace_read(evs, 32);   /* newest n events, ordered */
sai_trace_dump();                        /* console timeline */
```

This is the foundation for Tracealyzer-style timelines, the runtime
introspection stream and deterministic record/replay. Gated by
`CONFIG_SAI_TRACE`; budget accounting by `CONFIG_SAI_BUDGET` (both default
on).

The kernel ships a small user-space-style service layer on top of threads and
message queues, plus a multi-language script facility.

## Service framework (`sai/services.h`)

A *service* is a named thread with a message-queue inbox:

```c
sai_service_start("mysvc", my_loop, stack, stack_size, prio);
sai_service_post_named("mysvc", MSG_TOKEN);   /* producer side */
sai_service_stop(svc);                        /* posts 'STOP', waits for exit */
```

Service loops receive with `sai_service_recv()`; a stop request surfaces as
`SAI_ERR_STATE`, so the loop can `return`. Services are statically pooled
(4 slots) and appear in the kernel object registry.

Built-in services: `scriptd` (script execution), `statsd` (heartbeat), `sh`
(shell). Start them with `sai_script_service_start()`,
`sai_stats_service_start(ms)`, `sai_shell_start(line_fn, user, prio)`.

## Script service: MicroPython, C, C++, assembly, Rust

```c
sai_script_result_t res;
static char out[512];
res.output = out;  res.output_cap = sizeof(out);
sai_script_run(SAI_LANG_MICROPYTHON, "print('hi')", 12, &res, 0);
/* res.exit_code, res.duration_ms, res.output/res.output_len filled */
```

* **MicroPython** — vendored v1.25 core (`third_party/micropython`), 1 MB host /
  48 KB target GC heap, one global VM serialized by a mutex; the VM re-inits
  after an uncaught exception. Output is captured through a sink hook in the
  port (`sai_script_set_output_hook`).
* **C / C++ / asm / Rust** — `sai_lang_native_register(name, lang, src, len)`
  stores a snippet; on the *host* the platform toolchain (`clang`, `clang++`,
  `rustc`) compiles it to `%TEMP%` and runs it (child exit code = result).
  On the *target* there is no compiler: run pre-baked snippets or get
  `SAI_ERR_NOTSUP`.
* Execution is synchronous in the caller, or async via `sai_script_run_async()`
  (request posted to `scriptd`; poll `sai_script_status()`).

The `sai` Python module (`ports/micropython/modsai.c`) exposes
`sai.version()`, `sai.ticks_ms()`, `sai.sleep_ms()`, `sai.uptime()`,
`sai.ctx_switches()`.

## Other services

* **stats** — `sai_stats_get(&snap)`: ticks, context switches, ISR
  reschedules, threads, kobjects, devices, timers, heap free/max-block,
  script counters. `sai_stats_print()` renders it; `statsd` logs it
  periodically.
* **events** — named pub/sub channels over event flags:
  `sai_events_open/publish/wait` (publish is ISR-safe; 8 static channels).
* **softrpc** — `sai_softrpc_register(&{name, id, handler})` then
  `sai_softrpc_call(name, a0, a1, &ret)` from any thread or the shell.
* **shell** — commands: `help ps stats dev ev rpc run exec mp kill`.
  `run <lang> <src>` executes a payload; `mp <code>` is MicroPython
  shorthand; `exec <name>` runs a registered native snippet.

## Second-wave devices (`sai/devices2.h`)

* **PWM** (`sai_pwm_create/configure/enable/disable`) — kernel-timer software
  engine, 2 instances x 4 channels.
* **Buttons** (`sai_button_create/irq/read/subscribe/events`) — one-shot
  debounce timer, ISR-fed, press/release callback + event flags.
* **Loopback pairs** (`sai_loopback_create`) — two endpoints; writes on one
  appear on the other (mbox-notified reads with timeout).
* **Memory devices** (`sai_memdev_create`) — named scratch buffers with
  bounds-checked read/write.

All of them register through `sai_device_register()`, so the enumerator and
the shell `dev` command list them like any devicetree node.

## Tests

`test_micropython` (exec, capture, persistence, error recovery),
`test_script` (registry, sync/async, native C run, invalid args),
`test_services` (lifecycle, events, softrpc, stats),
`test_devices2` (pwm/button/loopback/mem + enumerator) — 12 suites total,
all green on the MSVC host build.
