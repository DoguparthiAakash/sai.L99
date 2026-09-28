# =====================================================================
# sai services: script/stats/events/softrpc + language runtimes
#
# Included from the root CMakeLists (host and target alike).  Depends on
# the `sai` target and the vendored MicroPython (sai_micropython).
# =====================================================================
set(SAI_SERVICES_DIR ${SAI_ROOT}/kernel/services)

add_library(sai_services STATIC
    ${SAI_SERVICES_DIR}/service.c
    ${SAI_SERVICES_DIR}/script.c
    ${SAI_SERVICES_DIR}/lang_registry.c
    ${SAI_SERVICES_DIR}/stats.c
    ${SAI_SERVICES_DIR}/events.c
    ${SAI_SERVICES_DIR}/softrpc.c
    ${SAI_ROOT}/lang/mp_backend.c
    ${SAI_ROOT}/lang/native_backend.c
    ${SAI_ROOT}/services/shell.c
)

target_include_directories(sai_services PUBLIC ${SAI_ROOT})
add_dependencies(sai_services sai_config)
target_link_libraries(sai_services PUBLIC sai)

if(MSVC)
    target_compile_options(sai_services PRIVATE /W4 /Zc:preprocessor)
else()
    target_compile_options(sai_services PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
# (MicroPython/MSVC shim headers arrive transitively via sai_micropython.)
