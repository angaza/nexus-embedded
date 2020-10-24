/** \file nexus_processing.c
 * \brief Implementation of product-side processing functions.
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "battery_res.h"
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
void process_nexus(void)
{
    uint32_t next_call_seconds;

    // initial Nexus Channel Core with initial uptime (in seconds)
    // approximately divide by 1000 (1024).
    nx_common_init((uint32_t)(k_uptime_get() >> 10));
    LOG_INF("Nexus successfully initialized\n");

    // initialize any Nexus Channel Core resources (in this case, 'battery')
    // after `nx_common_init`
    battery_res_init();

    while (1)
    {
        next_call_seconds = nx_common_process((uint32_t)(k_uptime_get() >> 10));
        // LED LD2 ON when ready/waiting for input
        LOG_INF(
            "Completed Nexus processing; will call `nx_common_process` again "
            "in %d seconds\n",
            next_call_seconds);
        k_msleep(next_call_seconds * 1000);
    }
}

// Run `process_nexus` as a standalone thread. It will sleep/idle when
// there is nothing to process.
K_THREAD_DEFINE(
    process_nexus_id, 5248, process_nexus, NULL, NULL, NULL, 5, 0, 0);

// wakes up `process_nexus` if processing is requested
void nxp_common_request_processing(void)
{
    k_wakeup(process_nexus_id);
}
