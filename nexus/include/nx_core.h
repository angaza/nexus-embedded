/** \file nx_core.h
 * \brief Core definitions and functions provided by Nexus.
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
 * operate without calling `nx_core_init` at startup, or calling
 * `nx_core_process` periodically.
 */

#ifndef _NEXUS__INC__NX_CORE_H_
#define _NEXUS__INC__NX_CORE_H_

#include "include/compiler_check.h"
#include "include/user_config.h"
#include <stdbool.h>
#include <stdint.h>

// Version of the overall embedded repository
#define NEXUS_EMBEDDED_VERSION_MAJOR 0
#define NEXUS_EMBEDDED_VERSION_MINOR 5
#define NEXUS_EMBEDDED_VERSION_PATCH 0
// typically stored as uint32, only
#define NEXUS_EMBEDDED_VERSION                                                 \
    ((NEXUS_EMBEDDED_VERSION_MAJOR << 16) |                                    \
     (NEXUS_EMBEDDED_VERSION_MINOR << 8) | (NEXUS_EMBEDDED_VERSION_PATCH))

//
// NONVOLATILE-RELATED
//

// statically allocate memory for RAM-resident NV block copies
#define NX_CORE_NV_BLOCK_0_LENGTH 8 // bytes
#define NX_CORE_NV_BLOCK_1_LENGTH 16 // bytes

#ifdef CONFIG_NEXUS_CHANNEL_ENABLED
// Nexus channel
#define NX_CORE_NV_BLOCK_2_LENGTH 10 // bytes
#define NX_CORE_NV_BLOCK_3_LENGTH 12 // bytes
// Blocks IDs 4-19 are reserved for established link data.
// One block for each link present.
// Always at least one present (block 4)
#define NX_CORE_NV_BLOCK_4_LENGTH 36 // 1 link
#if (CONFIG_NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS > 1) // 2 links
#define NX_CORE_NV_BLOCK_5_LENGTH NX_CORE_NV_BLOCK_4_LENGTH
#endif
#if (CONFIG_NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS > 2) // 3 links ... etc
#define NX_CORE_NV_BLOCK_6_LENGTH NX_CORE_NV_BLOCK_4_LENGTH
#endif
#if (CONFIG_NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS > 3)
#define NX_CORE_NV_BLOCK_7_LENGTH NX_CORE_NV_BLOCK_4_LENGTH
#endif
#if (CONFIG_NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS > 4)
#define NX_CORE_NV_BLOCK_8_LENGTH NX_CORE_NV_BLOCK_4_LENGTH
#endif
#if (CONFIG_NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS > 5)
#define NX_CORE_NV_BLOCK_9_LENGTH NX_CORE_NV_BLOCK_4_LENGTH
#endif
#if (CONFIG_NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS > 6)
#define NX_CORE_NV_BLOCK_10_LENGTH NX_CORE_NV_BLOCK_4_LENGTH
#endif
#if (CONFIG_NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS > 7)
#define NX_CORE_NV_BLOCK_11_LENGTH NX_CORE_NV_BLOCK_4_LENGTH
#endif
#if (CONFIG_NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS > 8)
#define NX_CORE_NV_BLOCK_12_LENGTH NX_CORE_NV_BLOCK_4_LENGTH
#endif
#if (CONFIG_NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS > 9) // 10 links
#define NX_CORE_NV_BLOCK_13_LENGTH NX_CORE_NV_BLOCK_4_LENGTH
#endif
#if (CONFIG_NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS > 10)
#error "More than 10 links requires additional NV configuration"
#endif

#define NX_CORE_NV_MAX_BLOCK_LENGTH NX_CORE_NV_BLOCK_4_LENGTH
#else
// keycode only
#define NX_CORE_NV_MAX_BLOCK_LENGTH NX_CORE_NV_BLOCK_1_LENGTH
#endif /* ifdef CONFIG_NEXUS_CHANNEL_ENABLED */

/** Nexus non-volatile data block metadata.
 *
 * Assumes uint16_t is 2 bytes wide, and uint8_t is 1 byte wide.
 */
struct nx_core_nv_block_meta
{
    uint16_t block_id;
    uint8_t length;
};

/** Check whether or not a given Nexus NV block is valid.
 *
 * \param block_meta metadata about the block to verify
 * \param full_block_data pointer to the first byte of data in the block
 */
bool nx_core_nv_block_valid(const struct nx_core_nv_block_meta block_meta,
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

/** IPV6 address format for Nexus IDs.
 *
 * Port is not specified and may be determined by implementing system.
 *
 * Possible scopes are:
 * - global_scope = true (globally valid IPv6 address with Nexus ARIN prefix)
 * - global_scope = false (address is link-local valid IPv6 address)
 * See also: nx_core_nx_id_to_ipv6_address
 */
typedef struct nx_ipv6_address
{
    uint8_t address[16];
    bool global_scope;
} nx_ipv6_address;

/** 16-byte secret key used for authenticating keycodes.
 *
 * Should be unique per device and assigned securely before production use.
 *
 * Must be packed as operations using this key expect the bytes to be
 * sequentially ordered, with no padding.
 */
NEXUS_PACKED_STRUCT nx_core_check_key
{
    uint8_t bytes[16];
};

/** Call at startup to initialize Nexus system and all enabled modules.
 *
 * Must be called before the Nexus System is ready for use.
 * Will initialize values, triggering reading of the latest values
 * from NV if available.
 *
 * \return void
 */
void nx_core_init(void);

/** Perform any 'long-running' Nexus Keycode operations.
 *
 * This function must be called within 20ms after `nxp_core_request_processing`
 * is called.
 *
 * Within this function, the Nexus Keycode library executes 'long-running'
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
uint32_t nx_core_process(uint32_t uptime_seconds);

/** Call before a planned shutdown event to safely store state and data of
 * Nexus modules.
 *
 * \return void
 */
void nx_core_shutdown(void);

/** Convert a Nexus ID into an IPV6 address.
 *
 * All Nexus IDs may be represented as valid link-local or globally valid
 * addresses depending on whether the Nexus ARIN global prefix is used. Link
 * local is sufficient for most use cases.
 *
 * This function returns a link-local address through the `dest_address`
 * pointer.
 *
 * The steps taken to convert a Nexus ID into an IPV6 address:
 *
 * 1) Represent `authority_id` as big-endian (network order).
 * 2) Represent `device_id` as big-endian (network order).
 * 3) Concatenate the leftmost/'first' byte of `device_id` to `authority_id`
 * 4) Treat this three byte block as an OUI and flip the 7th bit from the left
 * 5) Concatenate bytes [0xFF, 0xFE] to the three bytes already concatenated
 * 6) Concatenate the remaining three bytes of `device_id` to the five bytes
 * already concatenated
 * 7) Prepend [0xFE, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00] (0xFE80 is 'link
 * local' scope)
 * 8) The resulting 16-byte string is a valid link-local IPV6 address
 *
 * \param id nx_id to convert to IPV6 address
 * \param dest_address pointer to IPV6 address struct to populate
 * \return true if id is successfully converted into `dest_address`, else false
 */
bool nx_core_nx_id_to_ipv6_address(const struct nx_id* id,
                                   struct nx_ipv6_address* dest_address);

/** Convert an IPV6 address into a Nexus ID.
 *
 * Convert an IPV6 address representing a valid Nexus ID back into a Nexus
 * ID.
 *
 * This function will accept either global (Nexus ARIN prefix) or link-local
 * (FE:80) addresses that represent valid Nexus device IDs.
 *
 * In many cases, the IPV6 'address' of a Nexus device may be significantly
 * truncated (down to 2 bytes) in a link-local deployment, similar to 6LoWPAN
 * address compression.
 *
 * Will return false if the IPV6 address does not represent a Nexus device.
 *
 * \param address pointer to IPV6 address to convert to Nexus ID
 * \param id dest_id struct to populate with Nexus ID
 * \return true if `address` is successfully converted into Nexus ID, else false
 */
bool nx_core_ipv6_address_to_nx_id(const struct nx_ipv6_address* address,
                                   struct nx_id* dest_id);

#endif /* end of include guard: _NEXUS__INC__NX_CORE_H_ */
