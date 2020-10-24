/** \file nxp_keycode.h
 * \brief Platform interface required by the Nexus Keycode library.
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 *
 * Contains declarations of functions, enums, and structs that the Nexus
 * Keycode library uses to interface with port resources (the resources of
 * the platform that is using the library). The interface includes:
 *
 * * non-volatile memory reads and writes of persisted Nexus Keycode library
 * data
 * * user feedback in response to Nexus Keycode entry
 * * pay-as-you-go state, which some Nexus logic depends upon
 * * monotonic time, which is used by some time-dependent Nexus functions
 * * device identity, which is used to authenticate Nexus keycodes
 *
 * All port interfaces are included in this single header. Implementation
 * is necessarily platform-specific and must be completed by the manufacturer.
 */

#ifndef _NEXUS__INC__NXP_KEYCODE_H_
#define _NEXUS__INC__NXP_KEYCODE_H_

#include "include/nx_common.h"
#include "include/nx_keycode.h"

#ifdef __cplusplus
extern "C" {
#endif

/** User feedback interface.
 *
 * The Nexus library requires platform resources to indicate to users the
 * result of a particular Nexus keycode interaction, for example, pressing
 * a key or entering an entire keycode.
 */

/** Name of a specific user feedback pattern.
 */
enum nxp_keycode_feedback_type
{
    /** No feedback to user is requested.
     */
    NXP_KEYCODE_FEEDBACK_TYPE_NONE = 0,
    /** Request user feedback indicating 'invalid Nexus keycode received'.
     * For example, if the received keycode does not match any expected
     * formats.
     */
    NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_INVALID = 1,
    /** Request user feedback indicating 'valid Nexus keycode received,
     * but it has been received before and should not be applied'.
     */
    NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_VALID = 2,
    NXP_KEYCODE_FEEDBACK_TYPE_RESERVED = 3,
    /** Request user feedback indicating 'valid Nexus keycode received and
     * it should be applied'. For example, if user enters a valid keycode
     * that adds credit to the device.
     */
    NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_APPLIED = 4,
    /** Request user feedback indicating 'valid keypress received'.
     * Useful when user is entering keycode digit-by-digit.
     */
    NXP_KEYCODE_FEEDBACK_TYPE_KEY_ACCEPTED = 5,
    /** Request user feedback indicating 'invalid keypress received'.
     * Useful if, while entering a keycode, user enters a wrong key.
     */
    NXP_KEYCODE_FEEDBACK_TYPE_KEY_REJECTED = 6,
    /** Request user feedback displaying the internally-assigned device
     * serial number to be displayed. Could be communicated via an LED or
     * LCD display. Requires an interface to the module with awareness of
     * the serial number.
     */
    NXP_KEYCODE_FEEDBACK_TYPE_DISPLAY_SERIAL_ID = 7,
};

/** Asynchronously initiate specific user-feedback.
 *
 * This function is called by the Nexus library, which passes
 * a `nxp_keycode_feedback_type` value indicating the type of feedback which
 * should be signalled to the user. It is up to the manufacturer to
 * implement user feedback (LEDs, etc.) representing this feedback type.
 *
 * The initiation of any feedback pattern must be asynchronous: this
 * function must return immediately after starting the feedback pattern,
 * rather than waiting for completion of the pattern.
 *
 * If the product is currently displaying a feedback pattern and a second
 * call to `NXP_KEYCODE_FEEDBACK_start` is received, the device should
 * interrupt/end the current feedback pattern and begin the newly received
 * pattern. The patterns should not be queued.
 *
 * \par Example Scenario:
 * (For reference only. Actual implementation will differ based on platform)
 * -# Nexus library receives a valid keycode sequence
 * -# Nexus library instructs platform to display feedback pattern:
 *      - @code
 *      // (Inside Nexus library)
 *      NXP_KEYCODE_FEEDBACK_type to_display =
 * NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_VALID;
 *      NXP_KEYCODE_FEEDBACK_start(to_display);
 *      @endcode
 * -# `NXP_KEYCODE_FEEDBACK_start` initiates appropriate platform-specific
 * UI feedback.
 *      - @code
 *      // (Inside platform firmware, in body of
 * `NXP_KEYCODE_FEEDBACK_start` function) switch (script)
 *      {
 *          case NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_VALID:
 *              enable_output_PWM(output_LED); // platform enables LED
 * output display_LED_success_blinks(); // UI shows 'success' pattern break;
 *          // other cases here
 *      }
 *      return true;
 *      @endcode
 *
 * \warning May be called at interrupt time.
 * \return true if the user feedback was started successfully; false otherwise
 */
bool nxp_keycode_feedback_start(enum nxp_keycode_feedback_type feedback_type);

/** Add PAYG credit equal to `credit` amount.
 *
 * Add the specified amount of PAYG credit to the existing PAYG credit on
 * the device.
 *
 * If the device is currently in `PAYG_UNLOCKED`
 * state, then keycodes that add credit will *not* result in calls to this
 * function because they have no logical effect (you can't add more credit
 * to a device that has infinite credit).
 *
 * \return true if unit credit is added successfully, false otherwise.
 */
bool nxp_keycode_payg_credit_add(uint32_t credit);

/** Set PAYG credit to `credit` amount. All set credit keycodes will be
 * effective and result in this call, regardless of the current PAYG state.
 *
 * \return true if unit credit was set successfully, false otherwise.
 */
bool nxp_keycode_payg_credit_set(uint32_t credit);

/** Unlock device; never run out of credit. All unlock keycodes will be
 * effective and result in this call, regardless of the current PAYG state.
 *
 * Once unlocked, the only way to enter a "PAYG" mode again is to receive
 * a "SET CREDIT" keycode (which will trigger a call to
 * `nxp_keycode_payg_credit_set`).
 *
 * ADD CREDIT keycodes are ignored when the unit is unlocked.
 *
 * \return true if unit unlocked successfully, false otherwise
 */
bool nxp_keycode_payg_credit_unlock(void);

/** Platform identity interface. Allows Nexus Keycode to authenticate
 * keycodes and to perform functions that involve the device unique
 * identity.
 */

/** Return device-specific unique 16-byte authentication key.
 *
 * Return a copy of the device-unique secret key to use for keycode
 * authentication. The secret key must not change over the lifecycle of
 * the device.
 *
 * \return copy of permanent, 16-byte device-specific secret key
 */
struct nx_common_check_key nxp_keycode_get_secret_key(void);

/** Return the device-specific, user-facing serial ID.
 *
 * The full Nexus Keycode protocol uses this function to
 * allow users to 'punch in' an ID number, and respond with
 * 'VALID' if it matches the actual ID number of the device
 * (or 'REJECTED/INVALID' if it does not).
 *
 * \return integer representing device-specific, user-facing serial ID.
 */
uint32_t nxp_keycode_get_user_facing_id(void);

/** Called when an `nx_keycode_custom_flag` changes value.
 *
 * Provides the type of the flag that changed, as well as the new value.
 *
 * \param flag `nx_keycode_custom_flag` which changed
 * \param value new value of the flag (true or false)
 */
void nxp_keycode_notify_custom_flag_changed(enum nx_keycode_custom_flag flag,
                                            bool value);

//
// OPTIONAL "PASSTHROUGH" INTERFACE BELOW
// (Required for Nexus Channel)
// Used to pass Nexus Channel "Origin Command" keycodes onward
//

enum nxp_keycode_passthrough_application_subtype_id
{
    /* Reserved.
     */
    NXP_KEYCODE_PASSTHROUGH_APPLICATION_SUBTYPE_ID_RESERVED = 0,

    /* Passthrough data to be processed by Nexus Channel as an origin
     * command.
     */
    NXP_KEYCODE_PASSTHROUGH_APPLICATION_SUBTYPE_ID_NX_CHANNEL_ORIGIN_COMMAND =
        1,

    /* Pass arbitrary ASCII key values to the implementing application.
     */
    NXP_KEYCODE_PASSTHROUGH_APPLICATION_SUBTYPE_ID_PROD_ASCII_KEY = 2,
};

enum nxp_keycode_passthrough_error
{
    /** The invocation was successful; no error occurred.
     */
    NXP_KEYCODE_PASSTHROUGH_ERROR_NONE,

    /** The provided data is not recognized by the product, and was ignored.
     */
    NXP_KEYCODE_PASSTHROUGH_ERROR_DATA_UNRECOGNIZED,

    /** The provided data is recognized by the product, but has an
     * out-of-range value or size.
     */
    NXP_KEYCODE_PASSTHROUGH_ERROR_DATA_INVALID_VALUE_OR_SIZE,

    /** Catch-all error, which is used primarily in debugging.
     */
    NXP_KEYCODE_PASSTHROUGH_ERROR_UNKNOWN,
};

/**  Receive a passthrough keycode from the Nexus Keycode library.
 *
 * These keycodes represent product-specific commands, conveyed in the form
 * of a numeric keycode consisting of ASCII digits 0-9 inclusive.
 * Specifically, these are keycodes which are not related to "Device
 * Credit", and may convey other information. Thus, they are 'passed
 * through' to the application that handles the relevant logic.
 *
 * This is used to allow manufacturer-specific keycodes to be received by
 * the Nexus Keycode logic, and passed upwards to the product for further
 * processing.
 *
 * If the data received via this function is not recognized or understood by
 * the product code, the function must return
 * `NXP_KEYCODE_PASSTHROUGH_DATA_UNRECOGNIZED` or
 * `NXP_KEYCODE_PASSTHROUGH_DATA_INVALID_VALUE_OR_SIZE`. In these cases, the
 * Nexus
 * library will trigger `NXP_KEYCODE_FEEDBACK_SCRIPT_MESSAGE_INVALID` via
 * the `NXP_KEYCODE_FEEDBACK_start` interface.`
 *
 * If the product code does recognize and attempts to process the received
 * data, this function must return `NXP_KEYCODE_PASSTHROUGH_ERROR_NONE`. In
 * this case, the PAYG library will drive no UI response, and the product
 * must drive the appropriate UI response (if any).
 *
 * \param passthrough_keycode keycode data to pass through to application
 * \return NXP_KEYCODE_PASSTHROUGH_ERROR_NONE if data is recognized by the
 * product
 */
enum nxp_keycode_passthrough_error nxp_keycode_passthrough_keycode(
    const struct nx_keycode_complete_code* passthrough_keycode);

#ifdef __cplusplus
}
#endif

#endif /* end of include guard: _NEXUS__INC__NXP_KEYCODE_H_ */
