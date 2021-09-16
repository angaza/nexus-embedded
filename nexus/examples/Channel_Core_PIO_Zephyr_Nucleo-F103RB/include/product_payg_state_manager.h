/** \file product_payg_state_manager.h
 * \brief Example implementation of product-side PAYG state management
 * \author Angaza
 * \copyright 2021 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all
 * copies or substantial portions of the Software.
 *
 * Example of how to persist and manage PAYG state/credit. See also
 * 'nxp_implementations.c' for places where this information is consumed
 * by the Nexus library.
 */

#ifndef PRODUCT_PAYG_STATE_MANAGER__H
#define PRODUCT_PAYG_STATE_MANAGER__H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Amount of credit to store to indicate PAYG state is 'unlocked'
#define PRODUCT_PAYG_STATE_MANAGER_UNLOCKED_CREDIT_SENTINEL (UINT32_MAX)

/**
 * @brief product_payg_state_manager_init
 *
 * This will attempt to read any stored PAYG state values from
 * nonvolatile storage, and initialize them to defaults if nothing
 * is stored.
 *
 * Must be called *after* initializing nonvolatile storage (see
 * `flash_filesystem_init`).
 *
 * @return void
 */
void product_payg_state_manager_init(void);

/**
 * @brief payg_state_manager_add_credit
 *
 * Add PAYG credit to the existing credit on this device.
 *
 * Used by Nexus Keycode and Nexus Channel to modify credit on this device.
 *
 * @param[in] added_credit amount of PAYG credit to add
 *
 * @return void
 */
void product_payg_state_manager_add_credit(uint32_t added_credit);

/**
 * @brief payg_state_manager_set_credit
 *
 * Set PAYG credit on this device to a new value. Does not add to existing
 * credit.
 *
 * Used by Nexus Keycode and Nexus Channel to modify credit on this device.
 *
 * @param[in] set_credit amount of PAYG credit to add
 *
 * @return void
 */
void product_payg_state_manager_set_credit(uint32_t set_credit);

/**
 * @brief product_payg_state_manager_unlock
 *
 * "Unlock" the PAYG state on this device, so that the device is unrestricted
 * and does not 'count down' PAYG credit any longer.
 *
 * Subsequent calls to `payg_state_manager_set_credit` can be used to
 * reset the credit on this device (to 0, or any other value) and cause
 * the PAYG credit to start 'counting down' again.
 *
 * Used by Nexus Keycode and Nexus Channel to modify credit on this device.
 *
 * @return void
 */
void product_payg_state_manager_unlock(void);

/**
 * @brief product_payg_state_manager_get_current_credit
 *
 * Return the amount of current PAYG credit for this device.
 *
 * Special value 'UINT32_MAX' indicates device is PAYG unlocked
 * (see `payg_state_manager_unlock`).
 *
 * Used by Nexus Keycode and Nexus Channel to modify credit on this device.
 *
 * @return void
 */
uint32_t product_payg_state_manager_get_current_credit(void);

#ifdef __cplusplus
}
#endif
#endif // PRODUCT_PAYG_STATE_MANAGER__H