/** \file nonvol.h
 * \brief A mock implementation of nonvolatile storage for POSIX filesystems.
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

bool nv_init(void);

// Functions to write serial ID and secret key (not from Nexus)
bool prod_nv_read_identity(uint8_t length, void* read_buffer);
bool prod_nv_write_identity(uint8_t length, void* write_buffer);

// Functions to write PAYG state (not from Nexus)
bool prod_nv_read_payg_state(uint8_t length, void* read_buffer);
bool prod_nv_write_payg_state(uint8_t length, void* write_buffer);
