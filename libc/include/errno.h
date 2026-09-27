/**
 * @file libc/errno.h
 * @brief Freestanding errno.h shim mapping errno to sai_status_t.
 */
#ifndef __SAI_LIBC_ERRNO_H
#define __SAI_LIBC_ERRNO_H

#include <sai/types.h>

#define ENOENT   SAI_ERR_NOENT
#define ENOMEM   SAI_ERR_NOMEM
#define EACCES   SAI_ERR_PERM
#define EEXIST   SAI_ERR_STATE
#define EBUSY    SAI_ERR_BUSY
#define ETIMEDOUT SAI_ERR_TIMEOUT
#define EAGAIN   SAI_ERR_AGAIN
#define EINVAL   SAI_ERR_INVAL
#define ENOSPC   SAI_ERR_FULL
#define EIO      SAI_ERR_IO
#define ENOSYS   SAI_ERR_NOTSUP
#define EDEADLK  SAI_ERR_DEADLOCK

/** Kernel builds have no per-thread errno; expose a process-global slot. */
extern int sai_errno;
#define errno sai_errno

#endif
