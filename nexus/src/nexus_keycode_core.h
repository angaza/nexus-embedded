/** \file
 * Nexus Keycode Core Module (Header)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#ifndef NEXUS__KEYCODE__SRC__NEXUS_KEYCODE_CORE_H_
#define NEXUS__KEYCODE__SRC__NEXUS_KEYCODE_CORE_H_

#include "src/internal_keycode_config.h"

#if NEXUS_KEYCODE_ENABLED

#include "src/nexus_core_internal.h"
#include "src/nexus_keycode_pro.h"

#include <stdbool.h>
#include <stdint.h>

// Protocol-specific (small or full) initialization parameters
struct nexus_keycode_handling_config
{
    nexus_keycode_pro_parse_and_apply parse_and_apply;
    nexus_keycode_pro_protocol_init keycode_protocol_init;
    uint8_t stop_length;
    nx_keycode_key start_char;
    nx_keycode_key end_char;
    const nx_keycode_key* keycode_alphabet;
};

/** Initialize the Nexus Keycode module.
 *
 * Called on startup by `nx_core_init()`.
 */
void nexus_keycode_core_init(void);

/** Process any pending activity from Nexus keycode submodules.
 *
 * Called inside `nx_core_process()`.
 *
 * \param seconds_elapsed seconds since this function was previously called
 * \return seconds until this function must be called again
 */
uint32_t nexus_keycode_core_process(uint32_t seconds_elapsed);

/** Status of the Nexus Keycode Core module initialization
 *
 * \return true if initialized successfully, false otherwise
 */
bool nexus_keycode_core_init_completed(void);

/** Internal functions, not for calls outside this module.
 *
 * The `NEXUS_INTERNAL_IMPL_NON_STATIC` flag exposes these functions in
 * the header if in a unit testing scenario.
 */
#ifdef NEXUS_INTERNAL_IMPL_NON_STATIC
void _nexus_keycode_core_internal_init(
    const struct nexus_keycode_handling_config* config);
#endif

#endif /* if NEXUS_KEYCODE_ENABLED */
#endif /* ifndef NEXUS__KEYCODE__SRC__NEXUS_KEYCODE_CORE_H_ */
