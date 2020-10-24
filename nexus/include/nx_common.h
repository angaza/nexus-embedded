/** \file nx_common.h
 * \brief Definitions and functions common to all Nexus products.
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 *
 * This header includes functions and structs that are used internally by the
 * various Nexus systems (Keycode, Channel, etc) and that may also be used by
 * port code.
 *
 * Some of these must be used - for instance, no Nexus system will
 * operate without calling `nx_common_init` at startup, or calling
 * `nx_common_process` periodically.
 */

#ifndef _NEXUS__INC__NX_COMMON_H_
#define _NEXUS__INC__NX_COMMON_H_

#include "MODULE_VERSION.h"
#include "include/compiler_check.h"
#include "include/user_config.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// typically stored as uint32, only
// values from MODULE_VERSION.h
#define NEXUS_EMBEDDED_VERSION                                                 \
    ((NEXUS_EMBEDDED_VERSION_MAJOR << 16) |                                    \
     (NEXUS_EMBEDDED_VERSION_MINOR << 8) | (NEXUS_EMBEDDED_VERSION_PATCH))

//
// NONVOLATILE-RELATED
//

// statically allocate memory for RAM-resident NV block copies
#define NX_COMMON_NV_BLOCK_0_LENGTH 8 // bytes
#define NX_COMMON_NV_BLOCK_1_LENGTH 16 // bytes

#ifdef CONFIG_NEXUS_CHANNEL_LINK_SECURITY_ENABLED
    // Nexus channel link security (not common) requires NV storage
    #define NX_COMMON_NV_BLOCK_2_LENGTH 10 // bytes
    #define NX_COMMON_NV_BLOCK_3_LENGTH 12 // bytes
    // Blocks IDs 4-19 are reserved for established link data.
    // One block for each link present.
    // Always at least one present (block 4)
    #define NX_COMMON_NV_BLOCK_4_LENGTH 36 // 1 link
    #if (CONFIG_NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS > 1) // 2 links
        #define NX_COMMON_NV_BLOCK_5_LENGTH NX_COMMON_NV_BLOCK_4_LENGTH
    #endif
    #if (CONFIG_NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS > 2) // 3 links ... etc
        #define NX_COMMON_NV_BLOCK_6_LENGTH NX_COMMON_NV_BLOCK_4_LENGTH
    #endif
    #if (CONFIG_NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS > 3)
        #define NX_COMMON_NV_BLOCK_7_LENGTH NX_COMMON_NV_BLOCK_4_LENGTH
    #endif
    #if (CONFIG_NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS > 4)
        #define NX_COMMON_NV_BLOCK_8_LENGTH NX_COMMON_NV_BLOCK_4_LENGTH
    #endif
    #if (CONFIG_NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS > 5)
        #define NX_COMMON_NV_BLOCK_9_LENGTH NX_COMMON_NV_BLOCK_4_LENGTH
    #endif
    #if (CONFIG_NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS > 6)
        #define NX_COMMON_NV_BLOCK_10_LENGTH NX_COMMON_NV_BLOCK_4_LENGTH
    #endif
    #if (CONFIG_NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS > 7)
        #define NX_COMMON_NV_BLOCK_11_LENGTH NX_COMMON_NV_BLOCK_4_LENGTH
    #endif
    #if (CONFIG_NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS > 8)
        #define NX_COMMON_NV_BLOCK_12_LENGTH NX_COMMON_NV_BLOCK_4_LENGTH
    #endif
    #if (CONFIG_NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS > 9) // 10 links
        #define NX_COMMON_NV_BLOCK_13_LENGTH NX_COMMON_NV_BLOCK_4_LENGTH
    #endif
    #if (CONFIG_NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS > 10)
        #error "More than 10 links requires additional NV configuration"
    #endif

    #define NX_COMMON_NV_MAX_BLOCK_LENGTH NX_COMMON_NV_BLOCK_4_LENGTH
#else
    // keycode only
    #define NX_COMMON_NV_MAX_BLOCK_LENGTH NX_COMMON_NV_BLOCK_1_LENGTH
#endif /* ifdef CONFIG_NEXUS_CHANNEL_LINK_SECURITY_ENABLED */

/** Nexus non-volatile data block metadata.
 *
 * Assumes uint16_t is 2 bytes wide, and uint8_t is 1 byte wide.
 */
struct nx_common_nv_block_meta
{
    uint16_t block_id;
    uint8_t length;
};

/** Check whether or not a given Nexus NV block is valid.
 *
 * \param block_meta metadata about the block to verify
 * \param full_block_data pointer to the first byte of data in the block
 */
bool nx_common_nv_block_valid(const struct nx_common_nv_block_meta block_meta,
                              uint8_t* const full_block_data);

//
// CRYPTO, AUTH, INTEGRITY RELATED
//

/** Internally-used identity of a Nexus device.
 *
 * Always 6 bytes when packed.
 *
 * First two bytes are 'authority_id', which identifies the
 * entity which generated this Nexus ID.
 *
 * Last four bytes are 'device_id', which is a unique ID
 * for this Nexus device *among all other devices with IDs from the same
 * authority*.
 *
 * A given `nx_id` (combination of authority_id and device_id) is guaranteed
 * unique across all compliant Nexus devices in existence.
 *
 * Any Nexus ID may be converted to a valid link-local IPV6 address using
 * a standard expansion procedure similar to EUI-64.
 */
NEXUS_PACKED_STRUCT nx_id
{
    uint16_t authority_id;
    uint32_t device_id;
};

/** 16-byte secret key used for authenticating keycodes.
 *
 * Should be unique per device and assigned securely before production use.
 *
 * Must be packed as operations using this key expect the bytes to be
 * sequentially ordered, with no padding.
 */
NEXUS_PACKED_STRUCT nx_common_check_key
{
    uint8_t bytes[16];
};

/** Call at startup to initialize Nexus system and all enabled modules.
 *
 * Must be called before Nexus is ready for use.
 * Will initialize values, triggering reading of the latest values
 * from NV if available.
 *
 * \param initial_uptime_s current system uptime, in seconds. Should be
 * the same counter used to pass values into `nx_common_process`
 */
void nx_common_init(uint32_t initial_uptime_s);

/** Perform any 'long-running' Nexus operations.
 *
 * This function must be called within 20ms after
 * `nxp_common_request_processing` is called.
 *
 * Within this function, the Nexus library executes 'long-running'
 * operations that are not appropriate to run in an interrupt (such as
 * computing CRCs or hash results, and parsing or interpreting
 * entire keycodes).
 *
 * This function also drives the timeout and rate limiting logic (if used),
 * which is why `uptime_seconds` is required.
 *
 * The 'uptime_seconds' parameter must *never* go backwards, that is, uptime
 * must only increment.
 *
 * \param uptime_seconds current system uptime, in seconds.
 * \return maximum number of seconds to wait until `nx_keycode_process`
 * should be called again, based on the state of this module.
 */
uint32_t nx_common_process(uint32_t uptime_seconds);

/** Call before a planned shutdown event to safely store state and data of
 * Nexus modules.
 *
 */
void nx_common_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* end of include guard: _NEXUS__INC__NX_COMMON_H_ */
