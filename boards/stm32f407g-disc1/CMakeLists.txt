# boards/stm32f407g-disc1/CMakeLists.txt

set(SAI_BOARD_DIR ${CMAKE_CURRENT_LIST_DIR})

set(SAI_BOARD_SOURCES
    ${SAI_BOARD_DIR}/board.c
    ${SAI_BOARD_DIR}/startup.c
    ${SAI_BOARD_DIR}/vector_table.c
)

set(SAI_BOARD_LINKER_SCRIPT ${SAI_BOARD_DIR}/stm32f407.ld)
set(SAI_BOARD_DTS ${SAI_BOARD_DIR}/dts/stm32f407g-disc1.dts)

# Generate the devicetree header at configure time (host python).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    set(SAI_DTS_HEADER ${CMAKE_BINARY_DIR}/generated/devicetree.h)
    add_custom_command(
        OUTPUT ${SAI_DTS_HEADER}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/generated
        COMMAND ${Python3_EXECUTABLE} ${SAI_ROOT}/tools/dts2h.py
                ${SAI_BOARD_DTS} ${SAI_DTS_HEADER}
        DEPENDS ${SAI_BOARD_DTS} ${SAI_ROOT}/tools/dts2h.py
        COMMENT "Generating devicetree.h from ${SAI_BOARD_DTS}"
    )
    add_custom_target(sai_devicetree DEPENDS ${SAI_DTS_HEADER})
endif()
