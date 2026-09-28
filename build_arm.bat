@echo off
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64;%PATH%"
cd /d E:\sai.L99
if exist build-arm rmdir /s /q build-arm
cmake -S . -B build-arm -G "NMake Makefiles" -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-clang.cmake -DSAI_ARCH=arm -DSAI_BOARD=stm32f407g-disc1 -DSAI_CPU=cortex-m4 -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1
cmake --build build-arm
