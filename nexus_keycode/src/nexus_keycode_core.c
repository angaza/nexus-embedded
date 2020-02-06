/** \file
 * Nexus Keycode Core Module (Implementation)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "src/nexus_keycode_core.h"
#include "include/nexus_keycode_port.h"
#include "src/nexus_keycode_mas.h"
#include "src/nexus_keycode_pro.h"
#include "src/nexus_keycode_util.h"

/** Internal struct of data persisted to NV.
 */
static struct
{
    uint32_t uptime_s;
    bool init_completed;
    bool pending_init;
} _this;

const uint32_t NEXUS_KEYCODE_IDLE_TIME_BETWEEN_PROCESS_CALLS_SECONDS = 240;

// Default values - initialize based on the configuration settings
static const struct nexus_keycode_handling_config
    NEXUS_KEYCODE_HANDLING_CONFIG_DEFAULT = {
#if NEXUS_KEYCODE_PROTOCOL == NEXUS_KEYCODE_PROTOCOL_FULL
        nexus_keycode_pro_full_parse_and_apply,
        nexus_keycode_pro_full_init,
#elif NEXUS_KEYCODE_PROTOCOL == NEXUS_KEYCODE_PROTOCOL_SMALL
        nexus_keycode_pro_small_parse_and_apply,
        nexus_keycode_pro_small_init,
#else
#error "NEXUS_KEYCODE_PROTOCOL value unrecognized."
#endif
        NEXUS_KEYCODE_PROTOCOL_STOP_LENGTH, // stop_length
        NEXUS_KEYCODE_START_CHAR, // start_char
        NEXUS_KEYCODE_END_CHAR, // end_char
        NEXUS_KEYCODE_ALPHABET // keycode_alphabet
};

NEXUS_IMPL_STATIC void nexus_keycode_core_internal_init(
    const struct nexus_keycode_handling_config* config)
{
    _this.init_completed = false;
    _this.pending_init = true;
    _this.uptime_s = 0;

    // Provide protocol layer with:
    // * Function to call to parse and apply a completed frame
    // * Function to initialize any protocol-specific settings
    // * Valid keycode alphabet/character set
    nexus_keycode_pro_init(config->parse_and_apply,
                           config->keycode_protocol_init,
                           config->keycode_alphabet);

    // Provide message-assembly layer with function to handle completed frame
    nexus_keycode_mas_init(nexus_keycode_pro_enqueue);

    // Provide message-assembly layer with protocol-specific
    // start and end characters, and stop length (max message length)
    nexus_keycode_mas_bookend_init((const nx_keycode_key) config->start_char,
                                   (const nx_keycode_key) config->end_char,
                                   (const uint8_t) config->stop_length);
}

void nx_keycode_init(void)
{
    // Initialize using protocol-specific configuration (small or full)
    nexus_keycode_core_internal_init(&NEXUS_KEYCODE_HANDLING_CONFIG_DEFAULT);

    // Request for implementing system to call
    // 'nx_keycode_process' after calling `nx_keycode_init`, to initialize
    // the uptime seconds to the correct value.
    (void) port_request_processing();
}

uint32_t nx_keycode_process(uint32_t uptime_seconds)
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

    uint32_t min_sleep = NEXUS_KEYCODE_IDLE_TIME_BETWEEN_PROCESS_CALLS_SECONDS;

    min_sleep = u32min(min_sleep, nexus_keycode_mas_process(seconds_elapsed));
    min_sleep = u32min(min_sleep, nexus_keycode_pro_process());

    // System is initialized after first 'process' run
    // `pending_init` enforces call order (must call `init` then `process`)
    if (_this.pending_init)
    {
        _this.pending_init = false;
        _this.init_completed = true;
    }

    return min_sleep;
}

bool nexus_keycode_core_init_completed(void)
{
    return _this.init_completed;
}

uint32_t nexus_keycode_core_uptime(void)
{
    return _this.uptime_s;
}
