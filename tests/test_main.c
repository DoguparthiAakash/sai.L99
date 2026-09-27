/**
 * @file tests/test_main.c
 * @brief Shared main() for every unit test binary: boots the kernel on the
 *        host port and runs the registered suites *inside* the kernel's
 *        main thread so sleep/wake/schedule paths have a real current
 *        thread and a live tick source.
 */
#include "framework.h"
#include <sai/kernel.h>
#include <sai/host.h>

static void test_main_thread(void)
{
    int rc = sai_test_main();
    sai_host_set_exit_code(rc);
    /* Returning ends the kernel main thread: orderly shutdown via
     * port_main_returned() -> exit(exit_code). */
}

int main(void)
{
    if (sai_kernel_init() != SAI_OK) {
        return 2;
    }
    sai_host_atexit();
    sai_kernel_start(test_main_thread);    /* never returns */
    return 3;
}
