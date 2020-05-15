/** \file
 * Nexus Nonvolatile Module (Header)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#ifndef NEXUS__SRC__NEXUS_NV_INTERNAL_H_
#define NEXUS__SRC__NEXUS_NV_INTERNAL_H_

#include <stdbool.h>
#include <stdint.h>

#define NEXUS_NV_BLOCK_ID_WIDTH 2
#define NEXUS_NV_BLOCK_CRC_WIDTH 2
#define NEXUS_NV_BLOCK_WRAPPER_SIZE_BYTES 4
extern struct nx_core_nv_block_meta NX_NV_BLOCK_KEYCODE_MAS;
extern struct nx_core_nv_block_meta NX_NV_BLOCK_KEYCODE_PRO;
extern struct nx_core_nv_block_meta NX_NV_BLOCK_CHANNEL_LINK_HS_ACCESSORY;
extern struct nx_core_nv_block_meta NX_NV_BLOCK_CHANNEL_OM;
extern struct nx_core_nv_block_meta NX_NV_BLOCK_CHANNEL_LM_LINK_1;
extern struct nx_core_nv_block_meta NX_NV_BLOCK_CHANNEL_LM_LINK_2;
extern struct nx_core_nv_block_meta NX_NV_BLOCK_CHANNEL_LM_LINK_3;
extern struct nx_core_nv_block_meta NX_NV_BLOCK_CHANNEL_LM_LINK_4;
extern struct nx_core_nv_block_meta NX_NV_BLOCK_CHANNEL_LM_LINK_5;
extern struct nx_core_nv_block_meta NX_NV_BLOCK_CHANNEL_LM_LINK_6;
extern struct nx_core_nv_block_meta NX_NV_BLOCK_CHANNEL_LM_LINK_7;
extern struct nx_core_nv_block_meta NX_NV_BLOCK_CHANNEL_LM_LINK_8;
extern struct nx_core_nv_block_meta NX_NV_BLOCK_CHANNEL_LM_LINK_9;
extern struct nx_core_nv_block_meta NX_NV_BLOCK_CHANNEL_LM_LINK_10;

/** (Internal) Update a Nexus NV block.
 *
 * Nexus modules needing to store data to NV use this function to request that
 * the data is stored, without being concerned about the CRC.
 *
 * The block that is subsequently written via the `port` NV functions has
 * a block ID and CRC, but the data provided by the Nexus modules via
 * `inner_data` does **not** include this information.
 *
 * nexus_nv_update will not trigger a write if the block to be written
 * is identical to what is already stored in NV.
 *
 * **Note**: `inner_data` is *not* a pointer to a valid Nexus NV block! The
 * block ID and CRC are not included.
 *
 * \param block_meta metadata of block to update
 * \param data pointer to the first byte of data *contained in* the block.
 * `block_meta.length - NEXUS_NV_BLOCK_WRAPPER_SIZE_BYTES` will be copied from
 * inner data into the eventual block to be written.
 * \return true if the data are up to date in NV, false if the update failed
 */
bool nexus_nv_update(const struct nx_core_nv_block_meta block_meta,
                     uint8_t* inner_data);

/** (Internal) Read *inner/contained* data from a Nexus NV block
 *
 * Nexus modules needing to read blocks from the product side interface
 * wish to retrieve the data inside the blocks, not the CRC or the block ID.
 * This function abstracts away those elements.
 *
 * \param block_meta metadata of block to read
 * \param data pointer to where the data should be copied. No more than
 * `block_meta.length - NEXUS_NV_BLOCK_WRAPPER_SIZE_BYTES` bytes will be
 * copied to `data` (if there are data available; if there are no data,
 * then zero bytes will be copied to `data`)
 * \return true if the read is successful, false otherwise
 */
bool nexus_nv_read(const struct nx_core_nv_block_meta block_meta,
                   uint8_t* inner_data);

#endif /* ifndef NEXUS__SRC__NEXUS_NV_INTERNAL_H_ */
