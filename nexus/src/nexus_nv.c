/** \file
 * Nexus Nonvolatile Module (Implementation)
 * \author Angza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license.
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "src/nexus_nv.h"
#include "include/nxp_core.h"
#include "src/internal_keycode_config.h"
#include "utils/crc_ccitt.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
// block metadata structs
struct nx_core_nv_block_meta NX_NV_BLOCK_KEYCODE_MAS = {
    .block_id = 0, .length = NX_CORE_NV_BLOCK_0_LENGTH};
struct nx_core_nv_block_meta NX_NV_BLOCK_KEYCODE_PRO = {
    .block_id = 1, .length = NX_CORE_NV_BLOCK_1_LENGTH};

// Used internally to compute CRC given a pointer to start of a full block
uint16_t nexus_nv_compute_crc(const struct nx_core_nv_block_meta block_meta,
                              uint8_t* const full_block_data)
{
    NEXUS_ASSERT(block_meta.length > NEXUS_NV_BLOCK_CRC_WIDTH,
                 "Invalid NV block length");
    NEXUS_ASSERT(block_meta.length - NEXUS_NV_BLOCK_CRC_WIDTH < UINT8_MAX,
                 "Cannot compute CRC longer over more than 255 bytes");
    return compute_crc_ccitt(
        full_block_data,
        (uint8_t)(block_meta.length - NEXUS_NV_BLOCK_CRC_WIDTH));
}

bool nx_core_nv_block_valid(const struct nx_core_nv_block_meta block_meta,
                            uint8_t* const full_block_data)
{
    // check block ID: read first two bytes of `data`
    if (memcmp(&block_meta.block_id,
               full_block_data,
               NEXUS_NV_BLOCK_ID_WIDTH) != 0)
    {
        return false;
    }

    // check CRC: read last two bytes of `data`
    const uint8_t* crc_start_ptr =
        full_block_data + block_meta.length - NEXUS_NV_BLOCK_CRC_WIDTH;

    // calculate the check
    const uint16_t computed_crc =
        nexus_nv_compute_crc(block_meta, full_block_data);

    if (memcmp(&computed_crc, crc_start_ptr, NEXUS_NV_BLOCK_CRC_WIDTH) != 0)
    {
        return false;
    }

    return true;
}

bool nexus_nv_update(const struct nx_core_nv_block_meta block_meta,
                     uint8_t* inner_data)
{
    // read existing block from NV
    uint8_t existing_block[NX_CORE_NV_MAX_BLOCK_LENGTH] = {0};
    if (nxp_core_nv_read(block_meta, existing_block))
    {
        const uint8_t* old_inner_data =
            existing_block + NEXUS_NV_BLOCK_ID_WIDTH;
        if (memcmp(old_inner_data,
                   inner_data,
                   (uint8_t)(block_meta.length -
                             NEXUS_NV_BLOCK_WRAPPER_SIZE_BYTES)) == 0)
        {
            // do not write if the existing NV block is identical
            return true;
        }
    }

    const uint32_t inner_data_size =
        (uint32_t)(block_meta.length - NEXUS_NV_BLOCK_WRAPPER_SIZE_BYTES);
    NEXUS_ASSERT(inner_data_size < NX_CORE_NV_MAX_BLOCK_LENGTH,
                 "Invalid new block inner data size!");

    // compute a new valid "NX NV Block"
    uint8_t new_block[NX_CORE_NV_MAX_BLOCK_LENGTH] = {0};
    memcpy(new_block, &block_meta.block_id, NEXUS_NV_BLOCK_ID_WIDTH);
    memcpy(new_block + NEXUS_NV_BLOCK_ID_WIDTH, inner_data, inner_data_size);

    // compute and append the CRC, over the contents of the block ID and
    // inner data
    const uint16_t new_crc = compute_crc_ccitt(
        new_block, (uint8_t)(block_meta.length - NEXUS_NV_BLOCK_CRC_WIDTH));

    memcpy(new_block + NEXUS_NV_BLOCK_ID_WIDTH + inner_data_size,
           &new_crc,
           NEXUS_NV_BLOCK_CRC_WIDTH);

    // overwrite if the new block is valid and distinct
    return nxp_core_nv_write(block_meta, new_block);
}

bool nexus_nv_read(const struct nx_core_nv_block_meta block_meta,
                   uint8_t* inner_data)
{
    uint8_t block[NX_CORE_NV_MAX_BLOCK_LENGTH] = {0};

    nxp_core_nv_read(block_meta, block);
    if (nx_core_nv_block_valid(block_meta, block))
    {
        // Copy only the 'inner data', skip the CRC and block ID
        memcpy(
            inner_data,
            block + NEXUS_NV_BLOCK_ID_WIDTH,
            (uint8_t)(block_meta.length - NEXUS_NV_BLOCK_WRAPPER_SIZE_BYTES));
        return true;
    }
    return false;
}

// Internally, memory comparison to uint16_t values are performed, assuming that
// uint16_t values are 2 bytes wide.
NEXUS_STATIC_ASSERT(
    NEXUS_NV_BLOCK_ID_WIDTH ==
        sizeof(((struct nx_core_nv_block_meta*) 0)->block_id),
    "BLOCK_ID width not same size as block_meta block ID width.");
NEXUS_STATIC_ASSERT(NEXUS_NV_BLOCK_ID_WIDTH == sizeof(uint16_t),
                    "BLOCK_ID width not same size as uint16_t");
NEXUS_STATIC_ASSERT(NEXUS_NV_BLOCK_CRC_WIDTH == sizeof(uint16_t),
                    "CRC_WIDTH width not same size as uint16_t");
NEXUS_STATIC_ASSERT(NEXUS_NV_BLOCK_ID_WIDTH + NEXUS_NV_BLOCK_CRC_WIDTH ==
                        NEXUS_NV_BLOCK_WRAPPER_SIZE_BYTES,
                    "NEXUS_NV_BLOCK_WRAPPER_SIZE_BYTES not equal to sum of "
                    "block ID and CRC parts");
