# cmake/sai_micropython.cmake
#
# MicroPython (vendored, third_party/micropython) integration.
#
# Produces, in order:
#   1. genhdr/mpversion.h + genhdr/compressed.data.h   (static, pre-generated)
#   2. <build>/mp/qstr.i.last      one preprocessed stream of all MP sources
#   3. qstrdefs.collected.h / moduledefs.collected / root_pointers.collected
#   4. genhdr/qstrdefs.generated.h (via sai_qstr_wrap.py + makeqstrdata.py)
#   5. genhdr/moduledefs.h / genhdr/root_pointers.h
#   6. libsai_micropython (static lib: py core + port glue)
#
# Everything above runs before any py/*.c file is compiled (qstr.h embeds
# genhdr/qstrdefs.generated.h).

set(MP_DIR ${CMAKE_CURRENT_LIST_DIR}/../third_party/micropython)
set(MP_PORT_DIR ${CMAKE_CURRENT_LIST_DIR}/../ports/micropython)
set(MP_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/mp)
set(MP_GENHDR_DIR ${MP_BUILD_DIR}/genhdr)
set(MP_TOOLS_DIR ${MP_DIR}/tools)

set(SAI_PYTHON_EXECUTABLE ${Python3_EXECUTABLE})

if(SAI_HOST_BUILD)
    # MICROPY_NLR_SETJMP=1 on the host; no arch-specific compile-time
    # platform define is needed for the qstr pass.
    set(SAI_MP_PLATFORM_DEFINE "SAI_HOST_BUILD=1")
    set(SAI_MP_QSTR_EXTRA_FLAGS "")
else()
    # nlrthumb.c checks __thumb2__/__arm__ via --target=armv7em-none-eabi.
    set(SAI_MP_PLATFORM_DEFINE "SAI_ARM_V7M=1")
    set(SAI_MP_QSTR_EXTRA_FLAGS "")
endif()

function(sai_mp_cpp_args out)
    set(r
        -I${MP_DIR}
        -I${MP_DIR}/py
        -I${MP_DIR}/extmod
        -I${MP_PORT_DIR}
        -I${MP_GENHDR_DIR}
        -I${CMAKE_BINARY_DIR}/generated
        -I${SAI_ROOT}/include
        -I${SAI_ROOT}/libc/include
        -DNO_QSTR
    )
    if(MSVC)
        # POSIX-name shim headers (unistd.h etc.) from the MicroPython
        # windows port; clang must NOT see these.
        list(APPEND r -I${MP_PORT_DIR}/msvc)
    endif()
    set(${out} ${r} PARENT_SCOPE)
endfunction()

sai_mp_cpp_args(MP_CPP_ARGS)

# ---------------------------------------------------------------------
# Step 1: preprocess all qstr-bearing sources into one stream
# ---------------------------------------------------------------------
# All py core sources participate (nlr*.c excluded upstream by convention;
# makeqstrdefs only extracts from matching patterns so extra files are fine,
# but we mirror py.mk and skip nlr for tidiness).
set(MP_QSTR_SOURCES)
file(GLOB MP_PY_SOURCES ${MP_DIR}/py/*.c)
list(FILTER MP_PY_SOURCES EXCLUDE REGEX "/nlr[a-z0-9]*\\.c$")
list(APPEND MP_QSTR_SOURCES ${MP_PY_SOURCES})
list(APPEND MP_QSTR_SOURCES
    ${MP_PORT_DIR}/sai_mp_port.c
    ${MP_PORT_DIR}/modsai.c
)

set(MP_QSTR_I ${MP_BUILD_DIR}/qstr.i.last)

if(MSVC)
    # cl -E cannot emit multiple files to one stdout; wrap it per-file.
    set(MP_PP_COMMAND ${SAI_PYTHON_EXECUTABLE} ${SAI_ROOT}/tools/sai_cl_pp.py
                      ${CMAKE_C_COMPILER})
else()
    set(MP_PP_COMMAND ${CMAKE_C_COMPILER} -E)
endif()

add_custom_command(
    OUTPUT ${MP_QSTR_I}
    COMMAND ${SAI_PYTHON_EXECUTABLE} ${MP_TOOLS_DIR}/makeqstrdefs.py pp
            output ${MP_QSTR_I}
            pp ${MP_PP_COMMAND}
            cflags ${MP_CPP_ARGS} -D${SAI_MP_PLATFORM_DEFINE} ${SAI_MP_QSTR_EXTRA_FLAGS}
            sources ${MP_QSTR_SOURCES}
            changed_sources ${MP_QSTR_SOURCES}
    WORKING_DIRECTORY ${SAI_ROOT}
    DEPENDS ${MP_QSTR_SOURCES} ${MP_TOOLS_DIR}/makeqstrdefs.py
            ${SAI_ROOT}/tools/sai_cl_pp.py
    COMMENT "MP: preprocessing qstr sources"
    VERBATIM
)

# ---------------------------------------------------------------------
# Step 2: extract qstrs / module defs / root pointers
# ---------------------------------------------------------------------
set(MP_QSTR_SPLIT_STAMP ${MP_BUILD_DIR}/qstr.split)
set(MP_QSTR_COLLECTED ${MP_BUILD_DIR}/genhdr/qstrdefs.collected.h)
set(MP_MODULE_SPLIT_STAMP ${MP_BUILD_DIR}/moduledefs.split)
set(MP_MODULE_COLLECTED ${MP_BUILD_DIR}/moduledefs.collected)
set(MP_ROOT_SPLIT_STAMP ${MP_BUILD_DIR}/root_pointers.split)
set(MP_ROOT_COLLECTED ${MP_BUILD_DIR}/root_pointers.collected)

add_custom_command(
    OUTPUT ${MP_QSTR_SPLIT_STAMP}
    COMMAND ${SAI_PYTHON_EXECUTABLE} ${MP_TOOLS_DIR}/makeqstrdefs.py split
            qstr ${MP_QSTR_I} ${MP_BUILD_DIR}/qstr _
    COMMAND ${CMAKE_COMMAND} -E touch ${MP_QSTR_SPLIT_STAMP}
    DEPENDS ${MP_QSTR_I}
    COMMENT "MP: extracting qstrs"
    VERBATIM
)
add_custom_command(
    OUTPUT ${MP_QSTR_COLLECTED}
    COMMAND ${SAI_PYTHON_EXECUTABLE} ${MP_TOOLS_DIR}/makeqstrdefs.py cat
            qstr _ ${MP_BUILD_DIR}/qstr ${MP_QSTR_COLLECTED}
    DEPENDS ${MP_QSTR_SPLIT_STAMP}
    COMMENT "MP: collecting qstrs"
    VERBATIM
)

add_custom_command(
    OUTPUT ${MP_MODULE_SPLIT_STAMP}
    COMMAND ${SAI_PYTHON_EXECUTABLE} ${MP_TOOLS_DIR}/makeqstrdefs.py split
            module ${MP_QSTR_I} ${MP_BUILD_DIR}/module _
    COMMAND ${CMAKE_COMMAND} -E touch ${MP_MODULE_SPLIT_STAMP}
    DEPENDS ${MP_QSTR_I}
    COMMENT "MP: extracting module registrations"
    VERBATIM
)
add_custom_command(
    OUTPUT ${MP_MODULE_COLLECTED}
    COMMAND ${SAI_PYTHON_EXECUTABLE} ${MP_TOOLS_DIR}/makeqstrdefs.py cat
            module _ ${MP_BUILD_DIR}/module ${MP_MODULE_COLLECTED}
    DEPENDS ${MP_MODULE_SPLIT_STAMP}
    COMMENT "MP: collecting module registrations"
    VERBATIM
)

add_custom_command(
    OUTPUT ${MP_ROOT_SPLIT_STAMP}
    COMMAND ${SAI_PYTHON_EXECUTABLE} ${MP_TOOLS_DIR}/makeqstrdefs.py split
            root_pointer ${MP_QSTR_I} ${MP_BUILD_DIR}/root_pointer _
    COMMAND ${CMAKE_COMMAND} -E touch ${MP_ROOT_SPLIT_STAMP}
    DEPENDS ${MP_QSTR_I}
    COMMENT "MP: extracting root pointers"
    VERBATIM
)
add_custom_command(
    OUTPUT ${MP_ROOT_COLLECTED}
    COMMAND ${SAI_PYTHON_EXECUTABLE} ${MP_TOOLS_DIR}/makeqstrdefs.py cat
            root_pointer _ ${MP_BUILD_DIR}/root_pointer ${MP_ROOT_COLLECTED}
    DEPENDS ${MP_ROOT_SPLIT_STAMP}
    COMMENT "MP: collecting root pointers"
    VERBATIM
)

# ---------------------------------------------------------------------
# Step 3: qstrdefs.generated.h (wrap -> cpp -> unwrap -> makeqstrdata)
# ---------------------------------------------------------------------
set(MP_QSTRDEFS_GENERATED ${MP_GENHDR_DIR}/qstrdefs.generated.h)

add_custom_command(
    OUTPUT ${MP_QSTRDEFS_GENERATED}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MP_GENHDR_DIR}
    COMMAND ${SAI_PYTHON_EXECUTABLE} ${SAI_ROOT}/tools/sai_collect.py
            ${MP_BUILD_DIR}/qstrdefs.merged.h
            ${MP_DIR}/py/qstrdefs.h ${MP_QSTR_COLLECTED}
    COMMAND ${SAI_PYTHON_EXECUTABLE} ${SAI_ROOT}/tools/sai_qstr_wrap.py
            --qstrdefs ${MP_BUILD_DIR}/qstrdefs.merged.h
            -o ${MP_BUILD_DIR}/qstrdefs.preprocessed.h
    COMMAND ${SAI_PYTHON_EXECUTABLE} ${SAI_ROOT}/tools/sai_run_tool.py
            --output ${MP_QSTRDEFS_GENERATED}
            -- ${MP_DIR}/tools/makeqstrdata.py ${MP_BUILD_DIR}/qstrdefs.preprocessed.h
    WORKING_DIRECTORY ${SAI_ROOT}
    DEPENDS ${MP_QSTR_COLLECTED} ${MP_DIR}/py/qstrdefs.h
            ${SAI_ROOT}/tools/sai_qstr_wrap.py ${SAI_ROOT}/tools/sai_collect.py
    COMMENT "MP: generating qstrdefs.generated.h"
    VERBATIM
)

# ---------------------------------------------------------------------
# Step 4: moduledefs.h + root_pointers.h
# ---------------------------------------------------------------------
set(MP_MODULEDEFS_H ${MP_GENHDR_DIR}/moduledefs.h)
set(MP_ROOT_POINTERS_H ${MP_GENHDR_DIR}/root_pointers.h)

add_custom_command(
    OUTPUT ${MP_MODULEDEFS_H}
    COMMAND ${SAI_PYTHON_EXECUTABLE} ${SAI_ROOT}/tools/sai_run_tool.py
            --output ${MP_MODULEDEFS_H}
            -- ${MP_TOOLS_DIR}/makemoduledefs.py ${MP_MODULE_COLLECTED}
    DEPENDS ${MP_MODULE_COLLECTED} ${SAI_ROOT}/tools/sai_run_tool.py
    VERBATIM
)
add_custom_command(
    OUTPUT ${MP_ROOT_POINTERS_H}
    COMMAND ${SAI_PYTHON_EXECUTABLE} ${SAI_ROOT}/tools/sai_run_tool.py
            --output ${MP_ROOT_POINTERS_H}
            -- ${MP_TOOLS_DIR}/make_root_pointers.py ${MP_ROOT_COLLECTED}
    DEPENDS ${MP_ROOT_COLLECTED} ${SAI_ROOT}/tools/sai_run_tool.py
    VERBATIM
)

# ---------------------------------------------------------------------
# Step 5: the library
# ---------------------------------------------------------------------
set(MP_CORE_SOURCES ${MP_PY_SOURCES})
list(APPEND MP_CORE_SOURCES
    ${MP_DIR}/extmod/virtpin.c
    ${MP_PORT_DIR}/sai_mp_port.c
    ${MP_PORT_DIR}/modsai.c
)

add_custom_command(
    OUTPUT ${MP_GENHDR_DIR}/.stamp
    COMMAND ${CMAKE_COMMAND} -E touch ${MP_GENHDR_DIR}/.stamp
    DEPENDS ${MP_QSTRDEFS_GENERATED} ${MP_MODULEDEFS_H} ${MP_ROOT_POINTERS_H}
            ${MP_DIR}/genhdr/mpversion.h ${MP_DIR}/genhdr/compressed.data.h
    COMMENT "MP: genhdr ready"
    VERBATIM
)
add_custom_target(sai_mp_genhdr DEPENDS ${MP_GENHDR_DIR}/.stamp)

add_library(sai_micropython STATIC ${MP_CORE_SOURCES})
add_dependencies(sai_micropython sai_mp_genhdr sai_config)

target_include_directories(sai_micropython PUBLIC
    ${MP_DIR}
    ${MP_DIR}/py
    ${MP_DIR}/extmod
    ${MP_PORT_DIR}
    ${MP_GENHDR_DIR}
    ${MP_BUILD_DIR}
)
if(MSVC)
    target_include_directories(sai_micropython PUBLIC ${MP_PORT_DIR}/msvc)
endif()
target_include_directories(sai_micropython PRIVATE
    ${CMAKE_BINARY_DIR}/generated
    ${SAI_ROOT}/include
    ${SAI_ROOT}/libc/include
)
target_compile_definitions(sai_micropython PRIVATE _SAI_BUILDING)

if(SAI_HOST_BUILD)
    target_compile_definitions(sai_micropython PRIVATE SAI_HOST_BUILD=1)
else()
    target_compile_definitions(sai_micropython PRIVATE ${SAI_ARM_DEFS})
    target_compile_options(sai_micropython PRIVATE
        -mcpu=${SAI_CPU} -mthumb -ffreestanding -nostdlib)
endif()
