/** \file nexus_keycode_port.h
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

#ifndef __NEXUS__KEYCODE__INC__PORT_H_
#define __NEXUS__KEYCODE__INC__PORT_H_

#include "include/nx_keycode.h"
#include <stdbool.h>

/** Non-volatile memory interface.
 *
 * Some Nexus Keycode library features require persistence of data in order to
 * work properly. The declarations below define the interface to read and write
 * these data to non-volatile storage on the device.
 */

/** Writes new versions of Nexus Keycode library data to non-volatile (NV)
 * memory.
 *
 * A note on flash endurance:
 * This interface must allocate enough flash writes to last the entire product
 * lifecycle. Dedicating two flash pages to storage of these data can ensure
 * that this requirement can be met as well as add reliability. Specifically,
 * for typical flash storage — on which an entire page must be erased at once —
 * using two pages can prevent data corruption due to power loss while a page
 * is being erased.
 *
 * To preserve flash writes, it is recommended to perform a check to determine
 * if the write is necessary. Before attempting to write a data block, first
 * check the block ID (`block_id` in the `nx_keycode_nv_block_meta` struct).
 * Then check if a block with this ID is already stored in NV memory. If it is,
 * compare the contents of the new block with those of the most recent block
 * with the same ID. If they  are identical, do not write the contents to NV. If
 * they are different, proceed to write the block to NV.
 *
 * \par Example Scenario:
 * (For reference only.  Actual implementation will differ based on platform)
 * -# In this implementation of `port_nv_write`, platform firmware uses its
 *    internal NV write function (`nonvol_update_block`)
 *    - @code
 *      bool port_nv_write(
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
 *              return nonvol_update_block(block_meta.block_id, write_buffer);
 *          }
 *      }
 *      @endcode
 *
 * \note Never called at interrupt time.
 * \param block_meta metadata for Nexus NV block to write
 * \param write_buffer pointer to memory where data to write begins
 * \return true if data successfully written to NV, false otherwise
 */
bool port_nv_write(const struct nx_nv_block_meta block_meta,
                   void* write_buffer);

/** Reads the most recent version of Nexus Keycode data.
 *
 * \par Example Scenario:
 * (For reference only. Actual implementation will differ based on platform)
 *     - @code
 *       bool port_nv_read(
 *           struct nx_nv_block_meta block_meta,
 *           void* read_buffer)
 *       {
 *           memcpy(
 *               read_buffer,
 *               nx_nv_block_0_ram,
 *               block_meta.length;
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
bool port_nv_read(const struct nx_nv_block_meta block_meta, void* read_buffer);

/** User feedback interface.
 *
 * The Nexus library requires platform resources to indicate to users the
 * result of a particular Nexus keycode interaction, for example, pressing
 * a key or entering an entire keycode.
 */

/** Name of a specific user feedback pattern.
 */
enum port_feedback_type
{
    /** No feedback to user is requested.
     */
    PORT_FEEDBACK_TYPE_NONE = 0,
    /** Request user feedback indicating 'invalid Nexus keycode received'.
     * For example, if the received keycode does not match any expected formats.
     */
    PORT_FEEDBACK_TYPE_MESSAGE_INVALID = 1,
    /** Request user feedback indicating 'valid Nexus keycode received,
     * but it has been received before and should not be applied'.
     */
    PORT_FEEDBACK_TYPE_MESSAGE_VALID = 2,
    PORT_FEEDBACK_TYPE_RESERVED = 3,
    /** Request user feedback indicating 'valid Nexus keycode received and
     * it should be applied'. For example, if user enters a valid keycode that
     * adds credit to the device.
     */
    PORT_FEEDBACK_TYPE_MESSAGE_APPLIED = 4,
    /** Request user feedback indicating 'valid keypress received'.
     * Useful when user is entering keycode digit-by-digit.
     */
    PORT_FEEDBACK_TYPE_KEY_ACCEPTED = 5,
    /** Request user feedback indicating 'invalid keypress received'.
     * Useful if, while entering a keycode, user enters a wrong key.
     */
    PORT_FEEDBACK_TYPE_KEY_REJECTED = 6,
    /** Request user feedback displaying the internally-assigned device serial
     * number to be displayed. Could be communicated via an LED or LCD display.
     * Requires an interface to the module with awareness of the serial number.
     */
    PORT_FEEDBACK_TYPE_DISPLAY_SERIAL_ID = 7,
};

/** Asynchronously initiate specific user-feedback.
 *
 * This function is called by the Nexus library, which passes
 * a `port_feedback_type` value indicating the type of feedback which
 * should be signalled to the user. It is up to the manufacturer to
 * implement user feedback (LEDs, etc.) representing this feedback type.
 *
 * The initiation of any feedback pattern must be asynchronous: this function
 * must return immediately after starting the feedback pattern, rather than
 * waiting for completion of the pattern.
 *
 * If the product is currently displaying a feedback pattern and a second call
 * to `port_feedback_start` is received, the device should interrupt/end the
 * current feedback pattern and begin the newly received pattern. The patterns
 * should not be queued.
 *
 * \par Example Scenario:
 * (For reference only. Actual implementation will differ based on platform)
 * -# Nexus library receives a valid keycode sequence
 * -# Nexus library instructs platform to display feedback pattern:
 *      - @code
 *      // (Inside Nexus library)
 *      port_feedback_type to_display = PORT_FEEDBACK_TYPE_MESSAGE_VALID;
 *      port_feedback_start(to_display);
 *      @endcode
 * -# `port_feedback_start` initiates appropriate platform-specific UI feedback.
 *      - @code
 *      // (Inside platform firmware, in body of `port_feedback_start` function)
 *      switch (script)
 *      {
 *          case PORT_FEEDBACK_TYPE_MESSAGE_VALID:
 *              enable_output_PWM(output_LED); // platform enables LED output
 *              display_LED_success_blinks(); // UI shows 'success' pattern
 *              break;
 *          // other cases here
 *      }
 *      return true;
 *      @endcode
 *
 * \warning May be called at interrupt time.
 * \return true if the user feedback was started successfully; false otherwise
 */
bool port_feedback_start(enum port_feedback_type feedback_type);

/** Pay-as-you-go (PAYG) credit interface.
 * The Nexus Keycode library decodes keycodes containing PAYG-related data.
 * After successful decoding, it should alert the port code that is responsible
 * for PAYG updates that the state has been updated.
 */

/** PAYG enforcement state of the device.
 */
enum payg_state
{
    /** Unit functionality should be restricted.
     *
     * The unit has not been paid off and its payment period has elapsed.
     * Product functionality should be disabled or otherwise restricted.
     */
    PAYG_STATE_DISABLED,

    /** Unit functionality should be unrestricted.
     *
     * The unit has not yet been fully paid off, so eventually it will return
     * to PAYG_STATE_DISABLED state.
     */
    PAYG_STATE_ENABLED,

    /** Unit functionality should be unrestricted.
     *
     * The unit has been fully paid off, so will never become
     * PAYG_STATE_DISABLED.
     */
    PAYG_STATE_UNLOCKED
};

/** Add PAYG credit equal to `credit` amount.
 *
 * Add the specified amount of PAYG credit to the existing PAYG credit on
 * the device.
 *
 * If the device is currently in `PAYG_UNLOCKED`
 * state, then keycodes that add credit will *not* result in calls to this
 * function because they have no logical effect (you can't add more credit to
 * a device that has infinite credit).
 *
 * \return true if unit credit is added successfully, false otherwise.
 */
bool port_payg_credit_add(uint32_t credit);

/** Set PAYG credit to `credit` amount. All set credit keycodes will be
 * effective and result in this call, regardless of the current PAYG state.
 *
 * \return true if unit credit was set successfully, false otherwise.
 */
bool port_payg_credit_set(uint32_t credit);

/** Unlock device; never run out of credit. All unlock keycodes will be
 * effective and result in this call, regardless of the current PAYG state.
 *
 * Once unlocked, the only way to enter a "PAYG" mode again is to receive
 * a "SET CREDIT" keycode (which will trigger a call to `port_payg_credit_set`).
 *
 * ADD CREDIT keycodes are ignored when the unit is unlocked.
 *
 * \return true if unit unlocked successfully, false otherwise
 */
bool port_payg_credit_unlock(void);

/** Report current PAYG state of the device.
 */
enum payg_state port_payg_state_get_current(void);

/** Platform identity interface. Allows Nexus Keycode to authenticate keycodes
 * and to perform functions that involve the device unique identity.
 */

/** Return device-specific unique 16-byte authentication key.
 *
 * Return a copy of the device-unique secret key to use for keycode
 * authentication. The secret key must not change over the lifecycle of
 * the device.
 *
 * \return copy of permanent, 16-byte device-specific secret key
 */
struct nx_check_key port_identity_get_secret_key(void);

/** Return the device-specific, user-facing serial ID.
 *
 * \return integer representing device-specific, user-facing serial ID.
 */
uint32_t port_identity_get_serial_id(void);

/** Monotonic timekeeping interface.
 *
 * Some features of the Nexus Keycode library require additional processing
 * time and require the library to keep track of elapsed time. The functions in
 * this section allow the library to do these things.
 */

/** Product uptime since last reboot, in seconds.
 *
 * Called by the Nexus system to determine how many seconds have elapsed
 * since the last time an event occurred.
 *
 * \return seconds since the system has booted up
 */
uint32_t port_uptime_seconds(void);

/** Request to call `nx_keycode_process` outside of an interrupt context.
 *
 * Normally, keys are received and passed into the Nexus protocol from an
 * interrupt. If further processing is required, the Nexus protocol requests
 * that the processing occur outside of the interrupt context (to avoid
 * blocking critical product code).
 *
 * \return void
 */
void port_request_processing(void);

//
// OPTIONAL "PASSTHROUGH" INTERFACE BELOW
//

enum port_passthrough_error
{
    /** The invocation was successful; no error occurred.
     */
    PORT_PASSTHROUGH_ERROR_NONE,

    /** The provided data is not recognized by the product, and was ignored.
     */
    PORT_PASSTHROUGH_ERROR_DATA_UNRECOGNIZED,

    /** The provided data is recognized by the product, but has an out-of-range
     * value or size.
     */
    PORT_PASSTHROUGH_ERROR_DATA_INVALID_VALUE_OR_SIZE,

    /** Catch-all error, which is used primarily in debugging.
     */
    PORT_PASSTHROUGH_ERROR_UNKNOWN,
};

/**  Receive a passthrough keycode from the Nexus Keycode library.
 *
 * These keycodes represent product-specific commands, conveyed in the form
 * of a numeric keycode consisting of ASCII digits 0-9 inclusive. Specifically,
 * these are keycodes which are not related to "Device Credit", and may convey
 * other information. Thus, they are 'passed through' to the application that
 * handles the relevant logic.
 *
 * This is used to allow manufacturer-specific keycodes to be received by the
 * Nexus Keycode logic, and passed upwards to the product for further
 * processing.
 *
 * If the data received via this function is not recognized or understood by
 * the product code, the function must return
 * `PORT_PASSTHROUGH_DATA_UNRECOGNIZED` or
 * `PORT_PASSTHROUGH_DATA_INVALID_VALUE_OR_SIZE`. In these cases, the Nexus
 * library will trigger `PORT_FEEDBACK_SCRIPT_MESSAGE_INVALID` via
 * the `port_feedback_start` interface.`
 *
 * If the product code does recognize and attempts to process the received
 * data, this function must return `PORT_PASSTHROUGH_ERROR_NONE`. In this case,
 * the PAYG library will drive no UI response, and the product must drive the
 * appropriate UI response (if any).
 *
 * \param passthrough_keycode keycode data to pass through to application
 * \return PORT_PASSTHROUGH_ERROR_NONE if data is recognized by the product
 */
enum port_passthrough_error port_passthrough_keycode(
    const struct nx_keycode_complete_code* passthrough_keycode);

#endif
