/** \file
 * Nexus Core Internal Module (Implementation)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 *
 * This file implements the functions defined by
 * 'include/nx_core.h', and exposes other internal
 * information via `nexus_core_internal.h`.
 */

#include "include/nxp_core.h"
#include "include/nxp_keycode.h"
#include "src/nexus_channel_core.h"
#include "src/nexus_keycode_core.h"

/** Internal struct of data persisted to NV.
 */
static struct
{
    uint32_t uptime_s;
    bool init_completed;
    bool pending_init;
} _this;

const uint32_t NEXUS_CORE_IDLE_TIME_BETWEEN_PROCESS_CALL_SECONDS = 240;

void nx_core_init(void)
{
    _this.uptime_s = 0;
    _this.init_completed = false;
    _this.pending_init = true;

#if NEXUS_KEYCODE_ENABLED
    nexus_keycode_core_init();
#endif

#if NEXUS_CHANNEL_ENABLED
    nexus_channel_core_init();
#endif

    // Request for implementing system to call
    // 'nx_core_process' after calling `nx_core_init`, to initialize
    // the uptime seconds to the correct value.
    (void) nxp_core_request_processing();
}

uint32_t nx_core_process(uint32_t uptime_seconds)
{
    if (uptime_seconds < _this.uptime_s)
    {
        // Trigger an assert/abort in debug mode if this condition occurs
        NEXUS_ASSERT_FAIL_IN_DEBUG_ONLY(uptime_seconds >= _this.uptime_s,
                                        "Uptime cannot be in the past.");

        // Ask to be called again, with a valid number of uptime seconds
        return 0;
    }

    const uint32_t seconds_elapsed = uptime_seconds - _this.uptime_s;
    _this.uptime_s = uptime_seconds;

    uint32_t min_sleep = NEXUS_CORE_IDLE_TIME_BETWEEN_PROCESS_CALL_SECONDS;

#if NEXUS_KEYCODE_ENABLED
    min_sleep = u32min(min_sleep, nexus_keycode_core_process(seconds_elapsed));
#endif

#if NEXUS_CHANNEL_ENABLED
    min_sleep = u32min(min_sleep, nexus_channel_core_process(seconds_elapsed));
#endif

    // System is initialized after first 'process' run
    // `pending_init` enforces call order (must call `init` then `process`)
    if (_this.pending_init)
    {
        _this.pending_init = false;
        _this.init_completed = true;
    }

    return min_sleep;
}

bool nexus_core_init_completed(void)
{
    return _this.init_completed;
}

uint32_t nexus_core_uptime(void)
{
    return _this.uptime_s;
}

void nx_core_shutdown(void)
{
#if NEXUS_CHANNEL_ENABLED
    nexus_channel_core_shutdown();
#endif
}
