# sai.L99

**sai.L99** is a completely independent, general-purpose real-time operating system written from
scratch in C11 — not a fork of, and not built on top of, any existing kernel. It is architecturally
comparable to Zephyr RTOS: a preemptive priority scheduler, deterministic interrupt handling, a
modular driver framework, declarative build-time configuration, and clean separation between the
portable kernel core and the architecture/board layers.

## Highlights

- **Preemptive, priority-based scheduler** with 32 priority levels (0 = highest critical, 31 = idle),
  round-robin time slicing (per-priority, compile-time configurable), and an alternate
  **cooperative scheduling mode** (context switch only at yield/block points).
- **Threads**: static or dynamically allocated stacks, priorities, sleep/wake, join, delete,
  cooperative/inherited-priority helpers.
- **Synchronization**: mutexes with **priority inheritance**, counting semaphores, condition
  variables, interrupt-safe spinlocks (with ARM `LDREX/STREX` / host fallback backends).
- **IPC**: fixed-size message queues, byte mailboxes (ring buffer), byte pipes (backpressured),
  event flag groups (wait-any / wait-all, consume-on-exit).
- **Deterministic ISR-to-thread signaling**: drivers/ISRs call `sai_isr_*` entry points that never
  block and defer rescheduling to PendSV (ARM) or an IPI (host).
- **Memory**: static **memory pools** (fixed-block), **slab caches**, and a size-class
  **TLSF-style heap** with `O(1)` alloc/free and bounded fragmentation; optional poisoning and
  overflow guards; a tiny `malloc/free/calloc/realloc` shim over the heap.
- **Tickless idle**: idle thread arms a one-shot compare for the next timer deadline; on Cortex-M
  this uses SysTick or a 32-bit timer; on the host port it uses timer threads per CPU. On-target
  **power modes** (sleep/stop/standby) are selected from the next timer deadline.
- **HAL + ports**: everything arch-specific lives in `arch/<name>/` behind a small contract
  (`port.h`): context switch, critical sections, tick setup, stack init, idle hook. The kernel core
  never touches hardware registers.
- **Services** (`sai_services`): a named **service framework** (threads with message-queue
  inboxes + a stop protocol) hosting:
  - **script service** — runs payloads in **MicroPython** (vendored v1.25 VM, 48 KB target heap),
    **C**, **C++**, **assembly** and **Rust** (host toolchain compile+run; pre-baked snippets on
    target), with stdout capture and sync/async execution (`sai_script_run*`),
  - **stats service** — coherent kernel snapshot (`sai_stats_get`) + periodic heartbeat daemon,
  - **events service** — named pub/sub channels over event flags (`sai_events_publish/wait`),
  - **softrpc** — name → handler dispatch registry (`sai_softrpc_register/call`),
  - **shell** — console service (`run/exec/mp/ps/stats/dev/ev/rpc/kill` commands).
- **Devices, second wave**: software **PWM** (kernel-timer engine), debounced IRQ **buttons**
  with press/release callbacks, **loopback** endpoint pairs (IPC/test plumbing), and **memory**
  devices (named scratch buffers) — all registered in the standard device enumerator.
- **Driver framework**: refcounted `sai_device` nodes bound from a static **devicetree** blob,
  with UART/GPIO/SPI/I2C/timer APIs. A board = a tree fragment + config; adding a board never
  touches the kernel.
- **Config**: CMake build with a Kconfig-style declarative config (`saiconfig` text file) that
  gates features at compile time (`CONFIG_*` macros in `sai_config.h`).
- **Tests**: a host test framework (`tests/framework.h`) with 12 suites covering
  kernel lifecycle, scheduler, sync, IPC, memory, libc, ISR signaling, end-to-end integration,
  **MicroPython execution**, the **script service**, **services** (framework/events/rpc/stats)
  and the **second-wave devices**. Kernel primitives are validated on the native build
  (Windows/Linux) before hardware.
- **Samples**: `samples/blinky` (3 LED tasks + a UART shell thread with kernel stats),
  `samples/kernel_demo` (9 threads, deferred-ISR work, queues/mailboxes/pipes/event flags/
  mutexes/semaphores/condvars, heap/pool/slab, tickless idle) and `samples/lang_demo`
  (MicroPython + C + Rust payloads, events pub/sub, softrpc, then an interactive shell).
  The demo prints a full report (context switches, ISR reschedules, heap fragmentation) to the
  console — this is the primary proof artifact.

## Source tree

```
sai.L99/
├── kernel/            Portable kernel core (scheduler, threads, sync, IPC, memory, time)
├── arch/              Architecture ports
│   ├── host/          Native Windows/Linux port (threads-as-threads baton model)
│   └── arm/           ARM Cortex-M0+/M3/M4/M7 port (startup, PendSV/SVC/SysTick, faults)
│       ├── include/   arch/arm/mmap.h, cortex_m.h, toolchain overrides
│       ├── m0plus/    v6-M deltas (PRIMASK critical sections) — WIP, not a full port yet
│       ├── m4/        v7-M port: PendSV single-switch model, startup, fault dump
│       └── m7/        v7-M with D-cache maintenance + MPU linker section support
├── boards/            Board support packages (dts fragments, pinmux, config overlays)
│   └── stm32f407g-disc1
├── drivers/           Driver framework + serial/gpio/spi/i2c/timer/pwm/button/virtual classes
├── lang/              Language backends for the script service (MicroPython + native)
├── services/          Shell service
├── kernel/services/   Service framework, script/stats/events/softrpc services
├── include/sai/       Public API (types, kernel, time, sync, ipc, mem, log, device, services, port.h)
└── docs/              Architecture documentation
```

(Continued: `libc/`, `lib/`, `samples/`, `tests/`, `tools/` at the repo root; generated headers
(`sai/config.h`, `devicetree.h`) are produced into the build directory at configure time.)

## Building

Requirements: CMake ≥ 3.20, a C11 toolchain.
- **Host (native) build** — for unit tests and samples: MSVC, clang, or gcc.
- **Target build** — ARM Cortex-M: `clang` ≥ 16 (or `arm-none-eabi-gcc`), `llvm-lld` (or GNU ld),
  `llvm-objcopy`/`llvm-objdump` (or binutils equivalents).

### 1. Host (native) build — tests + samples

```sh
cmake -S . -B build
cmake --build build --config Release -j
ctest --test-dir build -C Release --output-on-failure   # 12 suites
```

On Windows the Visual Studio generator is used; test binaries land in `build\Release\` and the
samples in `build\bin\Release\`. Run the multi-language demo (MicroPython + C + Rust + shell):

```sh
.\build\bin\Release\lang_demo.exe
```

The demo runs 9 kernel threads (each with its own host thread), all IPC primitives, memory
pools/slab/heap, a tick thread, and tickless idle — and prints a periodic report. Run it for a few
seconds, then Ctrl-C:

```sh
.\build\bin\Release\kernel_demo.exe
.\build\bin\Release\blinky.exe
```

### 2. Target build — STM32F407 Discovery (Cortex-M4F)

```sh
cmake -S . -B build-stm32 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-clang.cmake \
      -DSAI_BOARD=stm32f407g-disc1 \
      -DSAI_CPU=cortex-m4
```

(`-DSAI_CPU=cortex-m4f` selects the hard-float variant; the core list lives in
`cmake/sai-arm.cmake`.)

Outputs `build-stm32/bin/blinky.elf/.bin/.hex` and `kernel_demo.elf/.bin/.hex`. Flash with
`st-flash write build-stm32/bin/blinky.bin 0x8000000` (or OpenOCD / STM32CubeProgrammer).

### 3. QEMU / no-hardware smoke test

QEMU's `netduinoplus2` machine (Cortex-M4) is close enough to the F407 for a scheduler/UART smoke
test:

```sh
cmake -S . -B build-qemu \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-clang.cmake \
      -DSAI_BOARD=stm32f407g-disc1 -DSAI_CPU=cortex-m4
cmake --build build-qemu -j
qemu-system-arm -M netduinoplus2 -nographic -kernel build-qemu/bin/blinky.elf
```

You should see periodic uptime-style output; typing into the emulated USART feeds the sample's
shell thread.

## Architecture overview

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

- The kernel core (`kernel/`) calls only the `port_*` functions declared in `include/sai/port.h`.
- An arch port implements those functions plus its own startup/interrupt entry. On Cortex-M
  threads run on PSP and **PendSV at the lowest exception priority is the single context-switch
  primitive** — including the very first dispatch at boot (single-switch-exception model;
  see `docs/architecture.md`). SysTick (or a 32-bit timer, for tickless) drives the kernel tick.
- Host port: each SAI thread runs on its own native thread; a depth-tracked "baton" mutex chooses
  the sole runner (the interrupt-masking equivalent), and ISRs are *simulated* by deferring work
  to a supervisor thread (`arch/host/port.c`); interrupts can also be raised from tests with
  `sai_host_raise_isr()`. This gives a deterministic, easy-to-debug scheduler that runs the exact
  same portable kernel code as on target.
- Determinism: no dynamic scheduling decisions in ISRs; all blocking waits have optional timeouts
  with bounded, auditable paths; timer callbacks run in the tick thread/ISR context with an
  explicit list; priority inheritance bounds priority inversion for mutexes; time slicing is
  per-priority and can be disabled per build for hard-RT builds.

## Porting to a new architecture

Implement the contract in `include/sai/port.h` (~15 functions) in `arch/<name>/port.c`, add a
`arch/<name>/startup` + fault dump, wire `SAI_ARCH` in the root `CMakeLists.txt`, and add a
Kconfig `choice ARCH` entry. RISC-V (RV32IMAC) and Xtensa ports slot in without kernel changes —
`kernel/` never includes arch headers.

## Porting to a new board

1. `boards/<new-board>/dts/<board>.dts` — describe the SoC + board (CPU, clocks, uart, gpio, ...).
2. `boards/<new-board>/CMakeLists.txt` — add SoC startup + driver instantiations + pinmux.
3. Optionally `boards/<new-board>/sai_board.conf` — feature defaults for this board.
4. Add the board to the root `CMakeLists.txt` (`set(SAI_VALID_BOARDS ...)`) and the Kconfig
   `choice BOARD` entry.
# This Project is made with the help of "AI" under MIT Licensing.
