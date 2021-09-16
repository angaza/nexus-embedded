/** \file user_pushbutton.h
 * \brief A user pushbutton demonstration
 * \author Angaza
 * \copyright 2021 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 *
 * This module configures a single pushbutton on the target board to
 * allow for a Nexus Channel "Accessory Reset" functionality.
 */

#ifndef USER_PUSHBUTTON__H
#define USER_PUSHBUTTON__H

#ifdef __cplusplus
extern "C" {
#endif

// Number of seconds to hold user pushbutton to
// trigger a Nexus Channel accesory 'link reset'.
#define USER_PUSHBUTTON_HOLD_TO_RESET_SECONDS 5

/** @brief user_pushbutton_init
 *
 * Initialize user pushbutton. Will configure the target
 * board to call `nx_channel_accessory_factory_reset` after
 * the user pushbutton has been held for `USER_PUSHBUTTON_HOLD_TO_RESET_SECONDS`.
 */
void user_pushbutton_init(void);

#ifdef __cplusplus
}
#endif
#endif // USER_PUSHBUTTON__H