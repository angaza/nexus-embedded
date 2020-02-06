/** \file nx_common.h
 * \brief Nexus system functions and structs shared by port and library code.
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 *
 *
 * This header includes functions and structs that are used internally by the
 * various Nexus systems (Keycode, Channel, etc) and that may also be used by
 * port code.
 */

#ifndef __NEXUS__COMMON__INC__NX_COMMON_H_
#define __NEXUS__COMMON__INC__NX_COMMON_H_

#include "include/common/siphash_24.h"
#include <stdbool.h>
#include <stdint.h>

// COMPILER-SPECIFIC MACROS
//
// The below options ensure that the 'packed' directly is correctly
// implemented based on the compiler in use. This section likely
// does not need to be modified, *unless* your preferred compiler is not
// detected below.
//
// **Warning** - commenting out or removing this section will prevent proper
// operation of the system.
//

// Keil/ARMCC
#if defined(__ARMCC_VERSION)
#define NEXUS_PACKED_STRUCT __packed struct
#define NEXUS_PACKED_UNION __packed union
// GNU gcc
#elif defined(__GNUC__)
#define NEXUS_PACKED_STRUCT struct __attribute__((packed))
#define NEXUS_PACKED_UNION union __attribute__((packed))
// IAR ICC
#elif defined(__IAR_SYSTEMS_ICC__)
#define NEXUS_PACKED_STRUCT __packed struct
#define NEXUS_PACKED_UNION __packed union
#else
#ifndef NEXUS_PACKED_STRUCT
#error                                                                         \
    "Unrecognized compiler. Defaults unavailable. Please define NEXUS_PACKED_STRUCT."
#endif
#ifndef NEXUS_PACKED_UNION
#error                                                                         \
    "Unrecognized compiler. Defaults unavailable. Please define NEXUS_PACKED_UNION."
#endif
#endif

//
// NONVOLATILE-RELATED
//

// statically allocate memory for RAM-resident NV block copies
#define NX_NV_BLOCK_0_LENGTH 8 // bytes
#define NX_NV_BLOCK_1_LENGTH 16 // bytes
#define NX_NV_MAX_BLOCK_LENGTH NX_NV_BLOCK_1_LENGTH

/** Nexus non-volatile data block metadata.
 *
 * Assumes uint16_t is 2 bytes wide, and uint8_t is 1 byte wide.
 */
struct nx_nv_block_meta
{
    uint16_t block_id;
    uint8_t length;
};

/** Check whether or not a given Nexus NV block is valid.
 *
 * \param block_meta metadata about the block to verify
 * \param full_block_data pointer to the first byte of data in the block
 */
bool nx_nv_block_valid(const struct nx_nv_block_meta block_meta,
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
NEXUS_PACKED_STRUCT nx_check_key
{
    uint8_t bytes[16];
};

#endif
