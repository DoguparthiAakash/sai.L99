/**
 * @file arch/host/port_trap.c
 * @brief Host port trap support: halt, abort and diagnostics tail.
 */
#include "port_internal.h"
#include <sai/host.h>

#include <stdlib.h>
#include <stdio.h>

void port_halt(void)
{
    fflush(stdout);
    /* Host "halt": stop the tick thread, then exit with failure code. */
    sai_host_shutdown();
    exit(70);                              /* EX_SOFTWARE */
}

void port_backtrace(void)
{
    /* Windows/POSIX backtraces differ; keep it simple for the host port. */
    printf("  (host backtrace: run under a debugger)\n");
}

void port_putchar(char c)
{
    fputc(c, stdout);
}
