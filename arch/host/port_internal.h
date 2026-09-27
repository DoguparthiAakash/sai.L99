/**
 * @file arch/host/port_internal.h
 * @brief Internal declarations shared by the host port sources.
 */
#ifndef SAI_HOST_PORT_INTERNAL_H
#define SAI_HOST_PORT_INTERNAL_H

#include <sai/types.h>
#include <sai/kernel.h>
#include <sai/time.h>
#include <sai/port.h>
#include <sai/host.h>
#include <string.h>

#define SAI_HOST_MAX_ISRS 16

#include "../../kernel/internal.h"

typedef struct {
    sai_host_isr_t fn;
    void *arg;
} host_isr_entry_t;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
DWORD WINAPI tick_thread_entry(LPVOID arg);
#else
void *tick_thread_entry(void *arg);
#endif

/** Stop the supervisor and exit the host process with the given code. */
void port_main_returned(void);
void sai_host_set_exit_code(int code);

#endif /* SAI_HOST_PORT_INTERNAL_H */
