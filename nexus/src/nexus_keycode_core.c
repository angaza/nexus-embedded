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

#if NEXUS_KEYCODE_ENABLED

#include "include/nxp_keycode.h"
#include "src/nexus_keycode_mas.h"
#include "src/nexus_keycode_pro.h"
#include "src/nexus_util.h"

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

static struct
{
    bool init_completed;
} _this;

NEXUS_IMPL_STATIC void _nexus_keycode_core_internal_init(
    const struct nexus_keycode_handling_config* config)
{
    _this.init_completed = false;

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

    _this.init_completed = true;
}

void nexus_keycode_core_init(void)
{
    // Initialize using protocol-specific configuration (small or full)
    _nexus_keycode_core_internal_init(&NEXUS_KEYCODE_HANDLING_CONFIG_DEFAULT);
}

uint32_t nexus_keycode_core_process(uint32_t seconds_elapsed)
{
    uint32_t min_sleep = NEXUS_CORE_IDLE_TIME_BETWEEN_PROCESS_CALL_SECONDS;

    // Call any keycode-related modules that require periodic processing
    min_sleep = u32min(min_sleep, nexus_keycode_mas_process(seconds_elapsed));
    min_sleep = u32min(min_sleep, nexus_keycode_pro_process());

    return min_sleep;
}

bool nexus_keycode_core_init_completed(void)
{
    return _this.init_completed;
}

#endif /* if NEXUS_KEYCODE_ENABLED */
