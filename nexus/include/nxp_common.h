/** \file nxp_common.h
 * \brief 'Common' interface required for any Nexus integration
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 *
 * Contains declarations of functions, enums, and structs that are common
 * and required across any Nexus integration.
 *
 * All functions beginning in `nxp_*` are ones which the implementing
 * product must implement.
 *
 * All declarations for `nxp_common` functions are included in this single
 * header. Implementation is necessarily platform-specific and must be
 * completed by the manufacturer.
 */

#ifndef _NEXUS__INC__NXP_COMMON_H_
#define _NEXUS__INC__NXP_COMMON_H_

#include "include/nx_common.h"

#ifdef __cplusplus
extern "C" {
#endif

//
// COMMON PROCESSING FUNCTIONALITY
//

/** Request to call `nx_common_process` outside of an interrupt context.
 *
 * See also: `nx_common.h`.
 *
 * When this is called by Nexus, the product *must* call
 * `nx_common_process` within 20ms. The product *should not*
 * call `nx_common_process` within the implementation of
 * `nxp_common_request_processing`.
 *
 * Normally, information may be passed into the Nexus modules from
 * an interrupt context (such as a keypress being passed in 'as it
 * is received' from the GPIO). To prevent long-running operations
 * inside interrupts, Nexus will defer long running actions
 * (like writing to NV, or performing cryptographic operations)
 * until `nx_common_process` is called.
 *
 */
void nxp_common_request_processing(void);

//
// NONVOLATILE MEMORY INTERFACE
//

/** Non-volatile memory interface.
 *
 * Some Nexus system features require persistence of data in order to
 * work properly. The declarations below define the interface to read and
 * write these data to non-volatile storage on the device.
 */

/** Writes new versions of Nexus systemdata to non-volatile (NV) memory.
 *
 * A note on flash endurance:
 * This interface must allocate enough flash writes to last the entire
 * product lifecycle. Dedicating two flash pages to storage of these data
 * can ensure that this requirement can be met as well as add reliability.
 * Specifically, for typical flash storage — on which an entire page must be
 * erased at once — using two pages can prevent data corruption due to power
 * loss while a page is being erased.
 *
 * To preserve flash writes, it is recommended to perform a check to
 * determine if the write is necessary. Before attempting to write a data
 * block, first check the block ID (`block_id` in the
 * `nx_keycode_nv_block_meta` struct). Then check if a block with this ID is
 * already stored in NV memory. If it is, compare the contents of the new
 * block with those of the most recent block with the same ID. If they  are
 * identical, do not write the contents to NV. If they are different,
 * proceed to write the block to NV.
 *
 * \par Example Scenario:
 * (For reference only.  Actual implementation will differ based on
 * platform)
 * -# In this implementation of `nxp_common_nv_write`, platform firmware uses
 * its internal NV write function (`nonvol_update_block`)
 *    - @code
 *      bool nxp_common_nv_write(
 *          struct nx_nv_block_meta block_meta,
 *          void* write_buffer)
 *      {
 *          // Check if existing data are identical
 *          bool identical = (bool) memcmp(nx_nv_block_0_ram, write_buffer,
 *                                         block_meta.length);
 *
 *          // Do not write if identical
 *          if (identical)
 *          {
 *              return true;
 *          }
 *          else
 *          {
 *              return nonvol_update_block(block_meta.block_id,
 * write_buffer);
 *          }
 *      }
 *      @endcode
 *
 * \note Never called at interrupt time.
 * \param block_meta metadata for Nexus NV block to write
 * \param write_buffer pointer to memory where data to write begins
 * \return true if data successfully written to NV, false otherwise
 */
bool nxp_common_nv_write(const struct nx_common_nv_block_meta block_meta,
                         void* write_buffer);

/** Reads the most recent version of Nexus nonvolatile data.
 *
 * \par Example Scenario:
 * (For reference only. Actual implementation will differ based on platform)
 *     - @code
 *       bool nxp_common_nv_read(
 *           struct nx_nv_block_meta block_meta,
 *           void* read_buffer)
 *       {
 *           memcpy(
 *               read_buffer,
 *               nx_nv_block_0_ram,
 *               block_meta.length);
 *
 *           return true;
 *       }
 *       @endcode
 *
 * \note Never called at interrupt time.
 * \param block_meta metadata for Nexus NV block to read
 * \param read_buffer pointer to where the read data should be copied
 * \return true if the read is succcessful, false otherwise
 */
bool nxp_common_nv_read(const struct nx_common_nv_block_meta block_meta,
                        void* read_buffer);

//
// Device "PAYG STATUS" READBACK INTERFACE
// Only used by Nexus Keycode and Nexus Channel (not Nexus Channel Core)
// configurations
//

#if defined(CONFIG_NEXUS_KEYCODE_ENABLED) ||                                   \
    defined(CONFIG_NEXUS_CHANNEL_USE_PAYG_CREDIT_RESOURCE)

/** Determine current PAYG state of the implementing device.
 *
 * Certain Nexus features and modules may use the existing device
 * PAYG state to determine whether to perform an action or not. For example,
 * the Nexus Keycode module will first detect whether a unit is already
 * unlocked before attempting to apply an ADD_CREDIT keycode to
 * that unit.
 */
enum nxp_common_payg_state
{
    /** Unit functionality should be restricted.
     *
     * The unit's PAYG credit has expired.
     * Product functionality should be disabled or otherwise restricted
     * until more PAYG credit is added or the unit is unlocked.
     */
    NXP_COMMON_PAYG_STATE_DISABLED,

    /** Unit functionality should be unrestricted.
     *
     * The unit will return to NXP_COMMON_PAYG_STATE_DISABLED state after
     * the PAYG credit expires.
     */
    NXP_COMMON_PAYG_STATE_ENABLED,

    /** Unit functionality should be unrestricted.
     *
     * The unit will not automatically disable, and will not become
     * NXP_COMMON_PAYG_STATE_DISABLED unless it receives a keycode/command
     * to go back into a PAYG ENABLED/DISABLED state.
     */
    NXP_COMMON_PAYG_STATE_UNLOCKED
};

/** Report current PAYG state of the device.
 *
 * \return current PAYG state of the device
 */
enum nxp_common_payg_state nxp_common_payg_state_get_current(void);

/* Retrieve the current remaining PAYG credit of this device.
 *
 * Used to display the remaining credit of this device to other Nexus
 * Channel devices. Currently only used in Nexus Channel devices.
 *
 * \return value of credit remaining
 */
uint32_t nxp_common_payg_credit_get_remaining(void);

#endif // defined(CONFIG_NEXUS_KEYCODE_ENABLED) ||
       // defined(CONFIG_NEXUS_CHANNEL_USE_PAYG_CREDIT_RESOURCE)

#ifdef __cplusplus
}
#endif

#endif /* end of include guard: _NEXUS__INC__NXP_COMMON_H_ */
