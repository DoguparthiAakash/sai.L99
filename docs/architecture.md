# sai.L99 Architecture

This document describes how sai.L99 is put together: the layering, the
scheduler's data model, the synchronization/IPC primitives, the memory
subsystem, and — in the most detail — the two ports that make the same
portable kernel run on a host OS and on bare-metal Cortex-M.

Companion reading: `README.md` (build + run), `include/sai/port.h` (the port
contract), `kernel/sched.c` (the scheduler, heavily commented).

---

## 1. Design goals

1. **Independence.** No code, headers, or design artifacts are taken from any
   existing RTOS; only the public, well-known semantics of the primitives
   (what a counting semaphore *does*) are reused, not their implementation.
2. **One kernel, two worlds.** The exact same `kernel/*.c` object code runs
   hosted on Windows/Linux (for tests and fast iteration) and freestanding on
   ARM Cortex-M. The kernel core contains zero `#ifdef SAI_HOST_BUILD` logic
   beyond the boot thread frames; all platform differences live behind the
   ~15-function `port.h` contract.
3. **Determinism over throughput.** Single scheduling decision point,
   priority inheritance, no blocking in ISRs, bounded wait paths.
4. **Auditable size.** The whole kernel core plus a UART driver links to a
   ~16 KB flash image on Cortex-M4 (`blinky`).

## 2. Layering

```
        samples/ (blinky, kernel_demo)
                 │  public API only (include/sai/*.h)
        kernel/  ←──────────────────────────  drivers/ (framework + classes)
            │                                       │
   include/sai/port.h  ←────────────────────  arch/<name>/port.c
            │                                       │
        kernel core is architecture-blind    arch-specific code (context switch,
                                             critical sections, tick, idle, fault)
```

- `kernel/` — the portable core: scheduler, threads, wait queues, time,
  synchronization (mutex/sem/condvar/spinlock), IPC (msgq/mbox/pipe/events),
  memory (mempool/slab/heap/kmem), logging, device registry, init.
- `include/sai/` — public API and the port contract.
- `arch/` — `host/` (baton-scheduler port) and `arm/` (`m4/` — the complete
  v7-M port; `m0plus/` currently v6-M deltas only, `m7/` cache/MPU extras).
- `boards/` — board support: vector table, early clock/GPIO/UART init,
  linker script, devicetree fragment, Kconfig-style config overlay.
- `drivers/` — the `sai_device` framework (registry keyed by devicetree node)
  plus class drivers: `serial/uart`, `gpio`, `spi`, `i2c`, `timer`.
- `libc/` — freestanding shims (`printf`, `string`, `stdlib`) used when the
  compiler's CRT is not available; on MSVC host builds the CRT is used.
- `tests/` — framework + 8 suites, all running on the host port.
- `tools/` — `kconfig.py` (config merger/codegen) and `dts2h.py`
  (devicetree → `devicetree.h`).

## 3. Scheduler

### 3.1 Data model: rotation rings

- 32 priorities, 0 = highest, 31 = idle (`SAI_NUM_PRIORITIES`).
- `_sai_ready[p]` is the head of a **circular** same-priority ready list;
  the head is the thread that has waited longest (next to run). Insert is
  O(1) at the tail, dispatch is O(1) at the head.
- A 32-bit bitmap (`_sai_ready_bitmap`) tracks non-empty rings; the next
  priority to run is found with a count-leading-zeros style scan
  (`_sai_ready_head()`), so picking is a single `clz` on ARM.
- The **running thread is not in any ring**. A thread is in exactly one of:
  ready ring, wait queue, deadline list — or dead/running.

### 3.2 One decision point: `_sai_schedule_pick()`

All scheduling decisions funnel through one function, called by the ports:

- The **runner keeps the CPU** unless:
  1. it blocked/died/slept (`state != RUNNING`),
  2. a **strictly higher** priority thread became ready,
  3. a same-priority **timeslice rotation** was requested
     (`_sai_rotate_prio`, set by `_sai_sched_tick()`).
- Otherwise no switch happens at all — an important latency property: a
  higher-priority thread that blocks again within one tick never evicts the
  runner.
- When a switch *is* needed and the previous thread is still RUNNING
  (preemption/timeslice — the "supervisor eviction" case), the pick re-queues
  it at the tail of its own ring. Threads that blocked were already dequeued
  by their own blocking path and are not re-queued.

### 3.3 Switch delivery

`_sai_schedule()` captures `prev = _sai_current`, calls `port_lock()`,
runs `_sai_schedule_pick()` (which updates `_sai_current`), and, if the pick
changed, calls `port_switch(prev, next)` **while still holding the lock** —
the switch itself releases it (see §6). ISRs never switch directly; they
raise a **preempt hint** (or pend PendSV on ARM), which is serviced at the
next `port_unlock()` tail or by the port's preemption service.

### 3.4 Boot and special threads

- `sai_kernel_init()` — heap, devices, tick state.
- `sai_kernel_start(main_fn)` — creates the static `main` thread (priority 1)
  running `main_fn` and the static `idle` thread (priority 31, kept **out**
  of the ready rings so "nothing ready" is unambiguous). The boot context
  becomes `_sai_current` and calls `port_start_first_thread()`; from then on
  the kernel never sees a thread without a proper context.
- `main_fn` returning triggers `port_main_returned()` — an orderly shutdown
  on the host (`exit()`), a parked core on target.

### 3.5 Modes

`SAI_SCHED_PREEMPTIVE` (default) and `SAI_SCHED_COOPERATIVE` (switches only
at yield/block points; timeslicing disabled). Selectable at run time with
`sai_sched_set_mode()`; the timeslice length is configurable per build and
per run.

## 4. Blocking: wait queues and deadlines

Every blocking primitive funnels into `_sai_wq_block(wq, key, timeout)`:

- Threads link onto a `sai_waitqueue` sorted by `wq_key` (FIFO or priority
  order — the primitive chooses).
- A waiter may request **any / all** semantics with **consume** semantics
  (event flags), a **timeout** (deadline list), or a **bare block**
  (`wq == NULL`, used by `join`).
- Wakeups deliver a result (`SAI_OK`, timeout, …) plus, for events, the
  satisfied flag mask (`wq_flags_out`).
- Timed waits register on a kernel deadline list (`wake_at`); `_sai_deadlines_tick()`
  wakes expired waiters from the tick/tickless path, which is what makes
  **tickless idle** possible: the idle loop just programs the next deadline
  as a one-shot timer (`_sai_next_deadline_ticks()`).

`join` is a bare block: the joiner links itself on the target's `join_next`
chain and blocks with no wait queue; thread exit walks the chain and unblocks
the joiners with `SAI_OK`.

## 5. Memory

- **`kernel/heap.c`** — TLSF-style two-level segregated fit: 32 first-level
  classes, second-level splits, O(1) alloc/free, rounding to split
  granularity on search, an 8-byte used guard block at region end so neighbor
  scans never run off, and an optional poison/overflow-guard mode. The
  `libc` `malloc/free/calloc/realloc` shim sits on top of it.
- **`kernel/mempool.c`** — fixed-block pools: O(1), no fragmentation,
  thread-safe, the workhorse for driver I/O buffers.
- **`kernel/slab.c`** — object caches over pools for same-size structs
  (e.g. TCBs).
- **`kernel/kmem.c`** — static backing region (`CONFIG_SAI_HEAP_SIZE`) and
  `sai_mem_init()`.

## 6. Port contract (`include/sai/port.h`)

| Function | Purpose |
|---|---|
| `port_init` | One-time port state (before any kernel activity) |
| `port_lock` / `port_unlock` | Critical sections; return/restore an opaque key |
| `port_switch(from, to)` | Context switch (see §7/§8) |
| `port_stack_init` | Shape an initial stack frame for a new thread |
| `port_thread_ready` | First-ready hook (host: spawn native thread; ARM: no-op) |
| `port_start_first_thread` | Enter scheduling at boot |
| `port_main_returned` | `main_fn` returned — orderly shutdown |
| `port_timer_setup` / `port_timer_oneshot` | Periodic tick / tickless one-shot |
| `port_idle_until_tick` | Idle: sleep until the next kernel deadline |
| `port_schedule_from_isr` | Request reschedule from interrupt context |
| `port_cycle_count`, `port_putchar`, `port_backtrace`, `port_halt` | Diagnostics |

## 7. Host port (`arch/host/`) — the baton model

Every SAI thread gets its own native thread (Windows threads / pthreads).
Exactly one native thread is the **runner** and holds the **baton** (a mutex)
— the equivalent of "the CPU". A per-thread record tracks the baton depth so
nested `port_lock()`s work like interrupt masking.

- `port_switch` = hand the baton to the target thread's record, park the
  outgoing thread on its condition variable, re-acquire when scheduled again.
- A **supervisor thread** owns wall-clock time (ticks), reaps dead threads,
  and performs "lazy" preemption: it only evicts the runner when the runner
  is parked, which keeps the model single-threaded and deterministic.
  (Documented limitation: a CPU-bound runner is only preempted when it
  yields, blocks, or at unlock tails — by design.)
- ISRs are simulated: `sai_host_raise_isr()` defers work to the supervisor
  context; tests can drive deterministic ISR timing.

The point is **debuggability**: the scheduler runs under a native debugger
with real threads and asserts, and every test suite exercises the identical
kernel code that ships on target.

## 8. ARM Cortex-M port (`arch/arm/`) — single-switch-exception model

v7-M (`m4/`) is the complete, hardware-verified port. `m0plus/` currently carries
only the v6-M *deltas* (PRIMASK critical sections, fault handling) — it does not
yet implement the full `port.h` contract, and a v6-M switch needs its own
design anyway (no BASEPRI means the PendSV rendezvous window used on v7-M is
not available; a PRIMASK port must pick a different switch protocol).

The v7-M port shares the same architecture:

- **Threads run on PSP**; handlers/kernel run on MSP. `CONTROL.SPSEL` is set
  by the first switch.
- **PendSV is the one switch primitive** (priority 0xFF, below the BASEPRI
  threshold): save `r4-r11` + PSP into `from->sp`, zero the lock nesting
  counter (the switch *is* the unlock), run `_sai_schedule_pick()`, restore
  `to`, open BASEPRI, exception-return into `to`.
- **`port_switch`** stores the pending target in `from->arch`, pends PendSV,
  and spins on `from->arch` — with **BASEPRI opened during the spin**, since
  PendSV must preempt a thread that is inside a critical section to switch
  it away. On return the caller's critical section is already released.
- **Boot**: `sai_kernel_start()` builds initial exception-style frames for
  `main` and `idle` (`port_stack_init`: xPSR/PC/LR/R12/R3-R0 + r4-r11), then
  `port_start_first_thread()` clears `_sai_current` and pends PendSV once;
  the handler dispatches the first thread with nothing to save. There is no
  SVC entry path anymore (the vector-table slot is a weak default).
- **Critical sections** use BASEPRI at `CONFIG_SAI_BASEPRI_THRESHOLD`
  (faults stay live); the M0 port uses PRIMASK. `s_critical_nesting` tracks
  depth and is zeroed by every switch.
- **Tick**: SysTick at threshold priority in periodic mode, one-shot in
  tickless mode; `port_idle_until_tick()` programs the next deadline and
  executes `wfi`.
- The TCB offset used by assembly (`SAI_TCB_SP_OFFSET = 60`) is pinned by
  `_Static_assert` against the real C layout in `arch/arm/m4/port.c` — a
  struct change cannot silently corrupt every switch.
- Fault handlers (`HardFault`, `MemManage`, `BusFault`, `UsageFault`) dump
  registers via the crash-safe polled console (`sai_printf_safe`).

## 9. Board support and drivers

A board is a directory under `boards/`: vector table, early init
(clock/GPIO/UART pinout), `stm32f407.ld`-style linker script, a devicetree
fragment (`dts/*.dts`) compiled by `tools/dts2h.py`, and a config overlay
(`sai_board.conf`) merged by `tools/kconfig.py` (order: defaults → user
`sai.conf` → board overlay).

Drivers bind against the generated devicetree: `sai_device_register()` nodes
are looked up by node id (`sai_device_get_by_node`), class drivers implement
the `sai_dev_ops_t` union (`uart`/`gpio`/`spi`/`i2c`/`timer`). The STM32F407
board exposes `console0` (USART2, RX-interrupt driven ring) and the four
user LEDs as a GPIO device. Adding a board never touches `kernel/`.

## 10. Testing strategy

- 8 host suites (`tests/`): kernel lifecycle, scheduler, sync primitives,
  IPC, memory, libc, ISR signaling, and an end-to-end integration test —
  28 test cases in total, all on the host port, all deterministic.
- `samples/kernel_demo` is the proof artifact: 9 threads exercising every
  primitive, periodic reports (context switches, ISR reschedules, heap
  fragmentation), tickless idle.
- Target builds compile the same kernel with `clang --target=armv7em` and
  link with `ld.lld` against the board linker script; firmware images are
  emitted as `.elf`/`.bin`/`.hex` with a size report.
