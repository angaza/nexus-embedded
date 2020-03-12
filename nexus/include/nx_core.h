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

//
// NONVOLATILE-RELATED
//

// statically allocate memory for RAM-resident NV block copies
#define NX_CORE_NV_BLOCK_0_LENGTH 8 // bytes
#define NX_CORE_NV_BLOCK_1_LENGTH 16 // bytes
#define NX_CORE_NV_MAX_BLOCK_LENGTH NX_CORE_NV_BLOCK_1_LENGTH

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

#endif /* end of include guard: _NEXUS__INC__NX_CORE_H_ */
