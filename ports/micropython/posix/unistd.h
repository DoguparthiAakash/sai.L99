/*
 * ports/micropython/posix/unistd.h
 * Minimal POSIX-name shim for the freestanding ARM target: the MicroPython
 * core includes <unistd.h> mainly for ssize_t.  Values follow the MP
 * bare-arm/ports conventions (no syscalls exist on this RTOS port).
 */
#ifndef SAI_MP_POSIX_UNISTD_H
#define SAI_MP_POSIX_UNISTD_H

#include <stdint.h>

typedef int32_t ssize_t;

#define STDIN_FILENO    0
#define STDOUT_FILENO   1
#define STDERR_FILENO   2

#define getpagesize() 4096

/* stream.c uses the POSIX seek constants via <unistd.h> on some toolchains */
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

#endif /* SAI_MP_POSIX_UNISTD_H */
