# cmake/toolchain-arm-clang.cmake
#
# Cross toolchain for ARM Cortex-M targets using clang + lld (or the
# arm-none-eabi binutils if clang is not present).
#
# Required cache variables (set by the board/CMake logic):
#   SAI_CPU        e.g. cortex-m4, cortex-m7, cortex-m0plus
#   SAI_CPU_FLAGS  e.g. "-mfpu=fpv4-sp-d16 -mfloat-abi=hard" for M4F
#
# SAI_MCPU_FLAGS are assembled by cmake/sai-arm.cmake; this file only wires
# the tools.

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

find_program(SAI_CLANG  clang  HINTS "$ENV{LLVM_ROOT}/bin")
find_program(SAI_LLVM_LD   ld.lld)
find_program(SAI_LLVM_OBJCOPY llvm-objcopy)
find_program(SAI_LLVM_OBJDUMP llvm-objdump)
find_program(SAI_GCC_OBJCOPY arm-none-eabi-objcopy)
find_program(SAI_GCC_OBJDUMP arm-none-eabi-objdump)
find_program(SAI_GCC_SIZE   arm-none-eabi-size)
find_program(SAI_LLVM_SIZE  llvm-size)

if(SAI_CLANG)
    set(CMAKE_C_COMPILER   "${SAI_CLANG}")
    set(CMAKE_ASM_COMPILER "${SAI_CLANG}")
    set(CMAKE_C_COMPILER_ID clang)
else()
    find_program(SAI_ARMCC arm-none-eabi-gcc)
    if(NOT SAI_ARMCC)
        message(FATAL_ERROR "Neither clang nor arm-none-eabi-gcc found")
    endif()
    set(CMAKE_C_COMPILER   "${SAI_ARMCC}")
    set(CMAKE_ASM_COMPILER "${SAI_ARMCC}")
endif()

if(SAI_LLVM_LD)
    set(CMAKE_LINKER       "${SAI_LLVM_LD}")
    set(CMAKE_C_LINK_EXECUTABLE "<CMAKE_LINKER> <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
elseif(NOT SAI_CLANG)
    set(CMAKE_LINKER "arm-none-eabi-ld")
endif()

if(SAI_LLVM_OBJCOPY)
    set(CMAKE_OBJCOPY "${SAI_LLVM_OBJCOPY}")
    set(CMAKE_OBJDUMP "${SAI_LLVM_OBJDUMP}")
    set(SAI_SIZE_TOOL "${SAI_LLVM_SIZE}")
elseif(SAI_GCC_OBJCOPY)
    set(CMAKE_OBJCOPY "${SAI_GCC_OBJCOPY}")
    set(CMAKE_OBJDUMP "${SAI_GCC_OBJDUMP}")
    set(SAI_SIZE_TOOL "${SAI_GCC_SIZE}")
endif()

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_FLAGS_INIT "--target=armv7em-none-eabi -mthumb -ffreestanding -nostdlib")
string(REPLACE "cortex-m4" "" _dummy "${SAI_CPU}")   # silence unused warnings
set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -mcpu=${SAI_CPU} ${SAI_MCPU_FLAGS}")

set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostdlib -Wl,--gc-sections")
