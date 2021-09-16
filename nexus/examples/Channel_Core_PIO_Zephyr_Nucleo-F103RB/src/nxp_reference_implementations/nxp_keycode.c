/* \file nxp_keycode.c
 * \brief Example implementation of functions specified by
 * 'nexus/include/nxp_keycode.h',
 * \author Angaza \copyright 2021 Angaza, Inc
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 *
 * Contains reference implementations of keycode-specific functions that
 * Nexus Library requires in order to function, such as:
 *
 * * Signals for product to display keycode entry feedback patterns
 * * Requests to unlock/set/add credit in response to successful keycode entry
 * * Request secret key to use for keycode verification
 * * Get 'user facing ID' for use in 'blinking out' ID to users (ID check
 * keycode)
 */

#include "nxp_keycode.h"

// for 'passthrough keycodes' that send data to Nexus Channel
#include "nx_channel.h"

// For memcpy
#include <string.h>

// Product-side code to access stored Nexus ID/key values
#include "product_nexus_identity.h"
// For interfacing with product managed PAYG credit storage
#include "product_payg_state_manager.h"

// Zephyr logging for easier demonstration purposes
#include <logging/log.h>
#include <zephyr.h>
LOG_MODULE_REGISTER(nxp_keycode);

// Keycode-specific functions
uint32_t nxp_keycode_get_user_facing_id(void)
{
    const struct nx_id* id_ptr = product_nexus_identity_get_nexus_id();
    // PAYG ID/user facing ID is the 'device ID' of the Nexus ID
    return id_ptr->device_id;
}

void nxp_keycode_notify_custom_flag_changed(enum nx_keycode_custom_flag flag,
                                            bool value)
{
    // XXX if using 'custom flag' functionality, implement this function
    // Most implementations will not need this functionality.
    (void) flag;
    (void) value;
}

struct nx_common_check_key nxp_keycode_get_secret_key(void)
{
    const struct nx_common_check_key* const keycode_key_ptr =
        product_nexus_identity_get_nexus_keycode_secret_key();
    return *keycode_key_ptr;
}

bool nxp_keycode_payg_credit_unlock(void)
{
    product_payg_state_manager_unlock();
    return true;
}

bool nxp_keycode_payg_credit_add(uint32_t credit)
{
    product_payg_state_manager_add_credit(credit);
    return true;
}

bool nxp_keycode_payg_credit_set(uint32_t credit)
{
    product_payg_state_manager_set_credit(credit);
    return true;
}

bool nxp_keycode_feedback_start(enum nxp_keycode_feedback_type feedback_type)
{
    switch (feedback_type)
    {
        case NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_INVALID:
            if (nx_keycode_is_rate_limited())
            {
                LOG_INF("Keycode rate limiting is active!");
            }
            else
            {
                LOG_INF("Invalid keycode");
            }
            break;

        case NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_VALID:
            LOG_INF("*OLD* keycode, not applied.");
            break;

        case NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_APPLIED:
            LOG_INF("*NEW* Keycode applied!");
            break;

        case NXP_KEYCODE_FEEDBACK_TYPE_KEY_ACCEPTED:
            LOG_INF("keypress accepted");
            break;

        case NXP_KEYCODE_FEEDBACK_TYPE_KEY_REJECTED:
            if (nx_keycode_is_rate_limited())
            {
                LOG_INF("Keycode rate limiting is active!");
            }
            else
            {
                LOG_INF("keypress rejected");
            }
            break;

        case NXP_KEYCODE_FEEDBACK_TYPE_DISPLAY_SERIAL_ID:
            LOG_INF("show user the PAYG ID: %d",
                    nxp_keycode_get_user_facing_id());
            break;

        case NXP_KEYCODE_FEEDBACK_TYPE_NONE:
            // no feedback required, ignore.
            break;

        default:
            LOG_INF("Unexpected!");
            // should not reach here
            break;
    }
    return true;
}

enum nxp_keycode_passthrough_error nxp_keycode_passthrough_keycode(
    const struct nx_keycode_complete_code* passthrough_keycode)
{
    // "passthrough" commands are how Nexus Channel origin commands
    // can be sent inside 'normal' Nexus Keycodes. The code below
    // can be copy/pasted directly to properly handle Nexus Channel
    // "Origin" commands.

    // Passthrough keycode contains the ASCII keys, get the subtype as int
    const uint8_t subtype_id = passthrough_keycode->keys[0] - 0x30;
    // length, excluding the subtype ID
    const uint8_t length = passthrough_keycode->length - 1;

    enum nxp_keycode_passthrough_error error_code =
        NXP_KEYCODE_PASSTHROUGH_ERROR_DATA_UNRECOGNIZED;

    switch (subtype_id)
    {
        case NXP_KEYCODE_PASSTHROUGH_APPLICATION_SUBTYPE_ID_NX_CHANNEL_ORIGIN_COMMAND:
            // pass the origin command to Nexus Channel for further processing
            // (create a Nexus Channel link, delete a Nexus Channel link, etc)
            // The response code from `nx_channel_handle_origin_command`
            // indicates whether the origin command was successfully
            // applied/accepted or not.
            {
                nx_channel_error origin_command_result =
                    nx_channel_handle_origin_command(
                        NX_CHANNEL_ORIGIN_COMMAND_BEARER_TYPE_ASCII_DIGITS,
                        (void*) &passthrough_keycode->keys[1],
                        length);
                error_code = NXP_KEYCODE_PASSTHROUGH_ERROR_NONE;

                if (origin_command_result != NX_CHANNEL_ERROR_NONE)
                {
                    // In a real product, display this result to the end-user
                    // via UI
                    LOG_INF("Nexus Channel origin command *rejected*.\n");
                }
                else
                {
                    // In a real product, display this result to the end-user
                    // via UI
                    LOG_INF("Nexus Channel origin command *accepted*.\n");
                }
            }
            break;

        case NXP_KEYCODE_PASSTHROUGH_APPLICATION_SUBTYPE_ID_PROD_ASCII_KEY:
            // intentional fallthrough. Manufacturers can implement
            // their own 'passthrough keycodes' if desired, which are handled
            // here.

        default:
            // not supported
            break;
    }

    return error_code;
}