# =====================================================================
# sai.L99 — independent RTOS
# Root build script.
#
# Host build (tests + samples):
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j
#   ctest --test-dir build --output-on-failure
#
# ARM target build (STM32F407 Discovery):
#   cmake -S . -B build-stm32 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-clang.cmake \
#         -DSAI_BOARD=stm32f407g-disc1 -DSAI_CPU=cortex-m4
#   cmake --build build-stm32 -j
# =====================================================================
cmake_minimum_required(VERSION 3.20)
project(sai_L99 C)

set(SAI_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# ---------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------
set(SAI_ARCH "host" CACHE STRING "Target architecture: host | arm")
set(SAI_BOARD "" CACHE STRING "Board name under boards/ (target builds)")
set(SAI_CPU "" CACHE STRING "CPU core, e.g. cortex-m4")
set(SAI_SOC "" CACHE STRING "SoC name, e.g. stm32f407xx")
set(SAI_VALID_BOARDS stm32f407g-disc1)

# ---------------------------------------------------------------------
# Generated configuration header (Kconfig-style)
# ---------------------------------------------------------------------
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(SAI_CONFIG_INPUTS ${SAI_ROOT}/sai.conf)
if(SAI_BOARD)
    list(APPEND SAI_CONFIG_INPUTS ${SAI_ROOT}/boards/${SAI_BOARD}/sai_board.conf)
endif()
if(SAI_ARCH STREQUAL "host")
    # host test builds enable the test framework
    set(SAI_CONFIG_EXTRA ${CMAKE_BINARY_DIR}/generated/sai_config_host.conf)
    file(WRITE ${SAI_CONFIG_EXTRA}
         "CONFIG_SAI_TESTS=y\nCONFIG_SAI_HOST_BUILD=y\n")
    list(APPEND SAI_CONFIG_INPUTS ${SAI_CONFIG_EXTRA})
endif()

set(SAI_CONFIG_HEADER ${CMAKE_BINARY_DIR}/generated/sai/config.h)
add_custom_command(
    OUTPUT ${SAI_CONFIG_HEADER}
    COMMAND ${Python3_EXECUTABLE} ${SAI_ROOT}/tools/kconfig.py
            ${SAI_ROOT}/cmake/sai_defaults.txt
            ${SAI_CONFIG_INPUTS}
            ${SAI_CONFIG_HEADER}
    DEPENDS ${SAI_ROOT}/tools/kconfig.py ${SAI_ROOT}/cmake/sai_defaults.txt
            ${SAI_CONFIG_INPUTS}
    COMMENT "Generating sai_config.h"
)
add_custom_target(sai_config ALL DEPENDS ${SAI_CONFIG_HEADER})

# ---------------------------------------------------------------------
# Compiler settings
# ---------------------------------------------------------------------
set(SAI_COMPILE_DEFS _SAI_BUILDING)

if(SAI_ARCH STREQUAL "host")
    set(SAI_HOST_BUILD ON)
    add_compile_definitions(${SAI_COMPILE_DEFS} SAI_HOST_BUILD=1)
    if(MSVC)
        add_compile_options(/W4 /Zc:preprocessor)
        # MSVC: disable noisy security warnings for the RTOS core
        add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    else()
        add_compile_options(-Wall -Wextra -Wno-unused-parameter)
    endif()
elseif(SAI_ARCH STREQUAL "arm")
    include(cmake/sai-arm.cmake)
    sai_arm_cpu_flags(${SAI_CPU} SAI_MCPU_FLAGS SAI_ARM_DEFS)
    add_compile_definitions(${SAI_COMPILE_DEFS} ${SAI_ARM_DEFS})
    add_compile_options(
        -mcpu=${SAI_CPU} -mthumb -ffreestanding -nostdlib
        -ffunction-sections -fdata-sections
        -Wall -Wextra -Wno-unused-parameter
    )
    add_link_options(-nostdlib -Wl,--gc-sections -T ${SAI_ROOT}/boards/${SAI_BOARD}/stm32f407.ld)
else()
    message(FATAL_ERROR "Unsupported SAI_ARCH '${SAI_ARCH}'")
endif()

# Generated include dir (config) first so libc shims win over system headers.
# NOTE: libc shims are added per-target (see sai target below); a global
# include_directories(libc/include) would shadow the MSVC CRT headers for
# the MicroPython target too (malloc/calloc redefinition C2375).
include_directories(
    ${CMAKE_BINARY_DIR}/generated
    ${SAI_ROOT}/include
    ${SAI_ROOT}/kernel
)

# ---------------------------------------------------------------------
# Core kernel + portable layers (static library: libsai)
# ---------------------------------------------------------------------
set(SAI_KERNEL_SOURCES
    kernel/sched.c
    kernel/thread.c
    kernel/wq.c
    kernel/time.c
    kernel/tickless.c
    kernel/mutex.c
    kernel/semaphore.c
    kernel/cond.c
    kernel/spinlock.c
    kernel/msgq.c
    kernel/mbox.c
    kernel/pipe.c
    kernel/event.c
    kernel/mempool.c
    kernel/slab.c
    kernel/heap.c
    kernel/kmem.c
    kernel/log.c
    kernel/device.c
    kernel/init.c
)

set(SAI_LIBC_SOURCES libc/printf.c)
if(NOT MSVC)
    # MSVC host builds use the CRT (memset etc. are compiler intrinsics
    # there); freestanding targets and non-MSVC hosts build our shims.
    list(APPEND SAI_LIBC_SOURCES libc/string.c libc/stdlib.c)
endif()

set(SAI_DRIVER_SOURCES
    drivers/serial/uart.c
    drivers/gpio/gpio.c
    drivers/spi/spi.c
    drivers/i2c/i2c.c
    drivers/timer/timer_dev.c
)

add_library(sai STATIC
    ${SAI_KERNEL_SOURCES}
    ${SAI_LIBC_SOURCES}
    ${SAI_DRIVER_SOURCES}
)
add_dependencies(sai sai_config)

if(SAI_HOST_BUILD)
    target_sources(sai PRIVATE
        arch/host/port.c
        arch/host/port_trap.c
    )
    target_include_directories(sai PRIVATE arch/host)
else()
    # arch + board sources for target builds
    if(SAI_CPU MATCHES "cortex-m0")
        target_sources(sai PRIVATE
            arch/arm/m0plus/port_m0.c
            arch/arm/m0plus/startup_m0.c
        )
    else()
        target_sources(sai PRIVATE
            arch/arm/m4/port.c
            arch/arm/m4/port_svcall.c
            arch/arm/m4/startup.c
            arch/arm/m4/context_switch.S
            arch/arm/m4/fault.c
        )
        if(SAI_CPU MATCHES "cortex-m7")
            target_sources(sai PRIVATE arch/arm/m7/cache_mpu.c)
        endif()
    endif()
    if(SAI_BOARD)
        include(boards/${SAI_BOARD}/CMakeLists.txt)
        target_sources(sai PRIVATE ${SAI_BOARD_SOURCES})
        set_target_properties(sai PROPERTIES LINK_DEPENDS ${SAI_BOARD_LINKER_SCRIPT})
        if(TARGET sai_devicetree)
            add_dependencies(sai sai_devicetree)
        endif()
    endif()
endif()

target_include_directories(sai PUBLIC
    ${SAI_ROOT}/include
    ${CMAKE_BINARY_DIR}/generated
)
if(NOT MSVC)
    # Freestanding/non-MSVC hosts link the sai libc shims; MSVC consumers use
    # the CRT (a PUBLIC libc/include would shadow the CRT for every target).
    target_include_directories(sai PUBLIC ${SAI_ROOT}/libc/include)
endif()

# ---------------------------------------------------------------------
# Networking stub
# ---------------------------------------------------------------------
if(SAI_NET OR CONFIG_SAI_NET)
    target_sources(sai PRIVATE lib/net/net.c)
endif()

# ---------------------------------------------------------------------
# MicroPython (vendored) -- built for host and target alike
# ---------------------------------------------------------------------
include(cmake/sai_micropython.cmake)
target_link_libraries(sai PUBLIC sai_micropython)

# ---------------------------------------------------------------------
# Host: tests + samples
# ---------------------------------------------------------------------
if(SAI_HOST_BUILD)

    enable_testing()

    # ---- test framework ---------------------------------------------
    add_library(sai_test STATIC
        tests/framework.c
    )
    target_include_directories(sai_test PUBLIC ${SAI_ROOT}/tests)
    target_link_libraries(sai_test PUBLIC sai)

    # ---- unit tests --------------------------------------------------
    file(GLOB SAI_TEST_SOURCES CONFIGURE_DEPENDS tests/test_*.c)
    list(FILTER SAI_TEST_SOURCES EXCLUDE REGEX "test_main.c")

    foreach(test_src ${SAI_TEST_SOURCES})
        get_filename_component(test_name ${test_src} NAME_WE)
        add_executable(test_${test_name} ${test_src} tests/test_main.c)
        target_link_libraries(test_${test_name} PRIVATE sai sai_test)
        add_test(NAME ${test_name} COMMAND test_${test_name})
        set_tests_properties(${test_name} PROPERTIES
            ENVIRONMENT "SAI_TEST=1"
            TIMEOUT 120
        )
    endforeach()

    # ---- samples ------------------------------------------------------
    add_executable(kernel_demo samples/kernel_demo/main.c)
    target_link_libraries(kernel_demo PRIVATE sai)
    add_executable(blinky samples/blinky/main.c)
    target_link_libraries(blinky PRIVATE sai)

    set_target_properties(kernel_demo blinky PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

else()

    # -----------------------------------------------------------------
    # Target: firmware images per sample
    # -----------------------------------------------------------------
    add_executable(blinky samples/blinky/main_target.c)
    target_link_libraries(blinky PRIVATE sai)
    add_executable(kernel_demo samples/kernel_demo/main_target.c)
    target_link_libraries(kernel_demo PRIVATE sai)

    foreach(fw blinky kernel_demo)
        target_link_options(${fw} PRIVATE
            "-Wl,-T,${SAI_ROOT}/boards/${SAI_BOARD}/stm32f407.ld"
            "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/${fw}.map")
        add_custom_command(TARGET ${fw} POST_BUILD
            COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${fw}>
                    ${CMAKE_CURRENT_BINARY_DIR}/bin/${fw}.bin
            COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${fw}>
                    ${CMAKE_CURRENT_BINARY_DIR}/bin/${fw}.hex
            COMMAND ${SAI_SIZE_TOOL} $<TARGET_FILE:${fw}>
            COMMENT "Generating ${fw}.bin/.hex + size report"
        )
    endforeach()

endif()
