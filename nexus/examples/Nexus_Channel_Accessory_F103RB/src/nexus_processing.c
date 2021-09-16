/** \file nexus_processing.c
 * \brief Implementation of product-side processing functions.
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "nx_common.h"
#include "nxp_common.h"
#include <zephyr.h>

// Zephyr logging for easier demonstration purposes
#include <logging/log.h>
LOG_MODULE_REGISTER(processing);

/** Thread to process Nexus-related activity.
 *
 * Should be put into a ready state when `nxp_common_request_processing`
 * is called.
 *
 * A thread based or RTOS approach is *not* required, but is used here
 * as an example. See the docstrings for `nx_common_init` and
 * `nx_common_process` for more info.
 */

K_SEM_DEFINE(immediate_processing_required_sem,
             0,
             1); // starts off 'not set/not avaiable'

void process_nexus(void)
{
    uint32_t next_call_seconds;

    // initial Nexus Channel Core with initial uptime (in seconds)
    // approximately divide by 1000 (1024).
    nx_common_init((uint32_t)(k_uptime_get() >> 10));
    LOG_INF("Nexus successfully initialized\n");

    while (1)
    {
        next_call_seconds = nx_common_process((uint32_t)(k_uptime_get() >> 10));

        // if we're able to take the semaphore, `k_sem_take` will return 0,
        // indicating that immediate processing is required.
        // Otherwise, sleep until next call seconds.
        if (k_sem_take(&immediate_processing_required_sem, K_NO_WAIT) != 0)
        {
            /*
            LOG_INF("Completed Nexus processing; will call `nx_common_process` "
                    "again "
                    "in %d seconds\n",
                    next_call_seconds);
            */
            k_msleep(next_call_seconds * 1000);
        }
    }
}

// Run `process_nexus` as a standalone thread. It will sleep/idle when
// there is nothing to process. Stack is sized to handle full Nexus Channel
// operation, and can be reduced by ~1k when using keycode only.
K_THREAD_DEFINE(
    process_nexus_th, 2560, process_nexus, NULL, NULL, NULL, 5, 0, 0);

// wakes up `process_nexus` if processing is requested
void nxp_common_request_processing(void)
{
    // set signal that immediate processing is required
    k_sem_give(&immediate_processing_required_sem);
    // wake up the process if it isn't already running. If it is already
    // awake, this has no effect.
    k_wakeup(process_nexus_th);
}
