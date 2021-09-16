/* \file nxp_common.c
 * \brief Example implementation of functions specified by
 * 'nexus/include/nxp_common.h'
 * \author Angaza \copyright 2021 Angaza, Inc
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 *
 * Contains reference implementations of 'common' functions that
 * Nexus Library requires in order to function (flash read/write, for instance).
 */

#include "nxp_common.h"

// Random number generation provided by Zephyr
#include <random/rand32.h>
// product-specific module for NV access
#include "flash_filesystem.h"
// To gain access to current PAYG state (stored/managed by product)
#include "product_payg_state_manager.h"

//
// 'nxp_common' functions
//

// Write a block to nonvolatile storage
bool nxp_common_nv_write(const struct nx_common_nv_block_meta block_meta,
                         void* write_buffer)
{
    int result = flash_filesystem_write_nexus_nv(
        block_meta.block_id, write_buffer, block_meta.length);
    if (result != block_meta.length)
    {
        // Failed to write the requested number of bytes with specified ID
        return false;
    }
    return true;
}

// Read a block from nonvolatile storage
bool nxp_common_nv_read(const struct nx_common_nv_block_meta block_meta,
                        void* read_buffer)
{
    int result = flash_filesystem_read_nexus_nv(
        block_meta.block_id, read_buffer, block_meta.length);
    if (result != block_meta.length)
    {
        // Failed to read the requested number bytes at specified ID
        return false;
    }
    return true;
}

enum nxp_common_payg_state nxp_common_payg_state_get_current(void)
{
    const uint32_t current_credit =
        product_payg_state_manager_get_current_credit();
    if (current_credit == PRODUCT_PAYG_STATE_MANAGER_UNLOCKED_CREDIT_SENTINEL)
    {
        return NXP_COMMON_PAYG_STATE_UNLOCKED;
    }
    else if (current_credit > 0)
    {
        return NXP_COMMON_PAYG_STATE_ENABLED;
    }

    // If not unlocked or enabled, device is disabled
    return NXP_COMMON_PAYG_STATE_DISABLED;
}

uint32_t nxp_common_payg_credit_get_remaining(void)
{
    return product_payg_state_manager_get_current_credit();
}