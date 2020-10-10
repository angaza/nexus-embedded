/** \file
 * Nexus Keycode Internal Configuration Parameters
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#ifndef NEXUS__KEYCODE__SRC__INTERNAL_KEYCODE_CONFIG_H_
#define NEXUS__KEYCODE__SRC__INTERNAL_KEYCODE_CONFIG_H_

// "nxp" will be included independently by modules that them.
#include "include/nx_keycode.h"
#include "src/internal_common_config.h"
#include "utils/crc_ccitt.h"
#include "utils/siphash_24.h"

// Convert user configuration to internal config
// 'CONFIG' prefix from Kconfig
#ifndef CONFIG_NEXUS_KEYCODE_ENABLED
    #define NEXUS_KEYCODE_ENABLED 0
#else
    #define NEXUS_KEYCODE_ENABLED 1

    // In most cases, there is no need to modify the values of this file.

    // Identifies the Nexus keycode protocol public 'release version'.
    #define NEXUS_KEYCODE_RELEASE_VERSION_COUNT 1

    #define NEXUS_KEYCODE_PROTOCOL_NO_STOP_LENGTH UINT8_MAX
    #define NEXUS_KEYCODE_UNDEFINED_END_CHAR '?'

    /** Fixed constant, do not edit */
    #define NEXUS_KEYCODE_PROTOCOL_FULL 1
    /** Fixed constant, do not edit */
    #define NEXUS_KEYCODE_PROTOCOL_SMALL 2

    // Convert user configuration to internal config.
    // 'CONFIG' prefix from Kconfig
    #ifdef CONFIG_NEXUS_KEYCODE_USE_FULL_KEYCODE_PROTOCOL
        #define NEXUS_KEYCODE_PROTOCOL NEXUS_KEYCODE_PROTOCOL_FULL
    #else
        #define NEXUS_KEYCODE_PROTOCOL NEXUS_KEYCODE_PROTOCOL_SMALL
    #endif

    #ifndef CONFIG_NEXUS_KEYCODE_ENABLE_FACTORY_QC_CODES
        #define NEXUS_KEYCODE_PRO_FACTORY_QC_LONG_LIFETIME_MAX 0
        #define NEXUS_KEYCODE_PRO_FACTORY_QC_SHORT_LIFETIME_MAX 0
    #else
        #define NEXUS_KEYCODE_PRO_FACTORY_QC_LONG_LIFETIME_MAX                 \
            CONFIG_NEXUS_KEYCODE_PRO_FACTORY_QC_LONG_LIFETIME_MAX
        #if NEXUS_KEYCODE_PROTOCOL == NEXUS_KEYCODE_PROTOCOL_FULL
            // 'short' OQC code is a special case, only defined if in largepad
            #define NEXUS_KEYCODE_PRO_FACTORY_QC_SHORT_LIFETIME_MAX            \
                CONFIG_NEXUS_KEYCODE_PRO_FACTORY_QC_SHORT_LIFETIME_MAX
        #else
            #define NEXUS_KEYCODE_PRO_FACTORY_QC_SHORT_LIFETIME_MAX 0
        #endif
    #endif

    #ifndef CONFIG_NEXUS_KEYCODE_RATE_LIMITING_ENABLED
        #define NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX 0
        #define NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT 0
        #define NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT 0
    #else
        #define NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX                   \
            CONFIG_NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX
        #define NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT         \
            CONFIG_NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT
        #define NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT   \
            CONFIG_NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT

    #endif

    // Not dependent on rate limiting, always defined
    #define NEXUS_KEYCODE_PROTOCOL_ENTRY_TIMEOUT_SECONDS                       \
        CONFIG_NEXUS_KEYCODE_PROTOCOL_ENTRY_TIMEOUT_SECONDS

    // Update the NEXUS_KEYCODE_PROTOCOL_STOP_LENGTH based on protocol
    #if NEXUS_KEYCODE_PROTOCOL == NEXUS_KEYCODE_PROTOCOL_FULL
        // Number of digits in a 'full' activation message
        #define NEXUS_KEYCODE_PROTOCOL_STOP_LENGTH                             \
            NEXUS_KEYCODE_PROTOCOL_NO_STOP_LENGTH
        #define NEXUS_KEYCODE_PROTOCOL_FULL_ACTIVATION_MESSAGE_LENGTH 14
        #define NEXUS_KEYCODE_START_CHAR '*'
        #define NEXUS_KEYCODE_END_CHAR '#'
        #define NEXUS_KEYCODE_ALPHABET "0123456789" // excluding start/end
    #else // small protocol
        #define NEXUS_KEYCODE_PROTOCOL_STOP_LENGTH 14
        #define NEXUS_KEYCODE_START_CHAR '1'
        #define NEXUS_KEYCODE_END_CHAR                                         \
            NEXUS_KEYCODE_UNDEFINED_END_CHAR // none/undefined for small
                                             // protocol
        #define NEXUS_KEYCODE_ALPHABET "2345" // excluding start/end
    #endif

    #if NEXUS_KEYCODE_PROTOCOL != NEXUS_KEYCODE_PROTOCOL_FULL
        #if NEXUS_KEYCODE_PROTOCOL != NEXUS_KEYCODE_PROTOCOL_SMALL
            #error "NEXUS_KEYCODE_PROTOCOL must be SMALL or FULL version."
        #endif
    #endif

    #if NEXUS_KEYCODE_PROTOCOL == NEXUS_KEYCODE_PROTOCOL_FULL
        #ifndef NEXUS_KEYCODE_PRO_FACTORY_QC_SHORT_LIFETIME_MAX
            #error                                                             \
                "NEXUS_KEYCODE_PRO_FACTORY_QC_SHORT_LIFETIME_MAX is undefined."
        #endif
        #ifndef NEXUS_KEYCODE_PRO_FACTORY_QC_LONG_LIFETIME_MAX
            #error                                                             \
                "NEXUS_KEYCODE_PRO_FACTORY_QC_LONG_LIFETIME_MAX is undefined."
        #endif

        #if NEXUS_KEYCODE_PRO_FACTORY_QC_SHORT_LIFETIME_MAX > 15
            #error                                                             \
                "NEXUS_KEYCODE_PRO_FACTORY_QC_SHORT_LIFETIME_MAX value is > 15."
        #endif
        #if NEXUS_KEYCODE_PRO_FACTORY_QC_LONG_LIFETIME_MAX > 15
            #error                                                             \
                "NEXUS_KEYCODE_PRO_FACTORY_QC_LONG_LIFETIME_MAX value is > 15."
        #endif
    #endif

    #if NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX != 0
        #ifndef NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT
            #error                                                             \
                "NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT is undefined."
        #endif
        #ifndef NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT
            #error                                                             \
                "NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT is undefined."
        #endif
        #if NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT == 0
            #error                                                             \
                "NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT must be nonzero."
        #endif
        #if NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX > 255
            #error                                                             \
                "NEXUS_KEYCODE_PRO_FACTORY_QC_LONG_LIFETIME_MAX value is > 255."
        #endif
        #if NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT > 255
            #error                                                             \
                "NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT value is > 255."
        #endif
        #if NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT > 3600
            #error                                                             \
                "NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT value is > 3600."
        #endif
    #endif

#endif /* ifndef CONFIG_NEXUS_KEYCODE_ENABLED */

// Compile-time parameter checks
#ifndef NEXUS_KEYCODE_ENABLED
    #error "NEXUS_KEYCODE_ENABLED must be defined."
#endif

#endif /* ifndef NEXUS__KEYCODE__SRC__INTERNAL_KEYCODE_CONFIG_H_ */
