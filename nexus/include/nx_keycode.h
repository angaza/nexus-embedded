/** \file nx_keycode.h
 * \brief Nexus Keycode functions and structs shared by port and library code.
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 *
 * This header includes functions and structs that are used internally by the
 * Nexus Keycode library and that may also be used by port code.
 */

#ifndef __NEXUS__INC__NX_KEYCODE_H_
#define __NEXUS__INC__NX_KEYCODE_H_

#include <stdbool.h>
#include <stdint.h>

//
// KEYCODE RELATED
//

/** Keypad key for input. Each key is a single character.
 *
 * Value should be the ASCII representation of the input key value
 * ('1', '2', '3', '*', "#', etc).
 */
typedef char nx_keycode_key;

/** Array of keycode keys making up a complete keycode.
 *
 *  Used when passing a keycode 'all at once' to the Nexus keycode processor
 *  instead of key-by-key.
 */
struct nx_keycode_complete_code
{
    nx_keycode_key* keys; /** Pointer to array of keycode keys. */
    uint8_t length; /** Number of keys to read from the `keys` pointer. */
};

/** Accept a single keypress entry as part of a Nexus keycode entry.
 *
 * This function may be called within an interrupt, as it will defer any
 * long running processing to the main loop. Example:
 *
 * - @code
 *   void system_handle_input_keypress(char input_character)
 *   {
 *      // Call this function after receiving an input character
 *      nx_keycode_handle_single_key((nx_keycode_key) input_character);
 *   }
 *   @endcode
 *
 *   Once an entire keycode has been received, the Nexus Keycode library
 *   will call the appropriate `payg_credit` and `feedback` functions
 *   based on the contents of the code (see `nxp_keycode.h`).
 *
 *   For example, after a keycode adding '5 days' of credit is received, Nexus
 *   Keycode logic will call the following two functions:
 *   @code
 *   // request to add 2 days of PAYG credit to product, in seconds
 *   nxp_keycode_payg_credit_add(172800);
 *
 *   // request to show 'accepted/applied' feedback to end user.
 *   nxp_keycode_feedback_start(PORT_FEEDBACK_TYPE_MESSAGE_APPLIED);
 *   @endcode
 *
 * \param key value of a single key being entered
 * \return true if key processing succeeded, false otherwise
 */
bool nx_keycode_handle_single_key(const nx_keycode_key key);

/** Receive an entire keycode and process it all at once.
 *
 * Accepts a 'complete keycode' (in the form of an `nx_keycode_complete_code`
 * struct), and will attempt to apply it. Example:
 *
 * - @code
 *   void system_handle_whole_keycode(char* keycode, length)
 *   {
 *      struct nx_keycode_complete_code nexus_keycode;
 *      nexus_keycode.keys = keycode;
 *      nexus_keycode.length = length;
 *
 *      nx_keycode_handle_complete_keycode(&nexus_keycode);
 *   }
 *   @endcode
 *
 * This function should not be called from within an interrupt, as there is
 * blocking processing (such as applying the keycode, and potentially NV
 * writes) which will occur.
 *
 * \param keycode struct representing an entire keycode and its length
 * \return true if keycode processing succeeded, false otherwise
 */
bool nx_keycode_handle_complete_keycode(
    const struct nx_keycode_complete_code* keycode);

/** Determine if keycode rate-limiting is active.
 *
 * This can be called after receiving a `NXP_KEYCODE_FEEDBACK_TYPE_KEY_REJECTED`
 * or `NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_INVALID` feedback display request from
 * `nxp_keycode_feedback_start` in order to provide additional context
 * to the user as to why the key/keycode was rejected.
 *
 * \return true if keycode rate-limiting is active, false otherwise
 */
bool nx_keycode_is_rate_limited(void);

#endif /* end of include guard: __NEXUS__INC__NX_KEYCODE_H_ */
