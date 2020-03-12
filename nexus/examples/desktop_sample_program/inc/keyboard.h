/**
 * @file keyboard.h
 * @author Angaza
 * @date 25 February 2020
 * @brief file containing example Nexus Keycode entry code implementation.
 *
 * This file exposes two ways to send keycodes to the Nexus Keycode library.
 * Keycodes can be sent 'key-by-key' or 'all at once' if desired. The default
 * implementation is to send it 'all at once'.
 *
 * Nexus keycodes are entered by the user, and can:
 * - Set, add, update credit to a specified amount
 * - Unlock/Relock a device
 * - Perform other functions (e.g., test messages, display serial ID)
 *
 * The product code implementing the Nexus Keycode library does not interpret
 * or parse keycodes, but instead passes them directly to the Nexus Keycode
 * library, which properly updates its internal state based on the keycode.
 */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdio.h>

/* @brief Used to clear out the input stream used for keycode input.
 *
 * This is used in the example program to clear the `stdin` buffer when
 * the program starts up.
 */
void keyboard_init();

/* @brief Prompt the user for a keycode and accept the keys.
 *
 * This function asks the user to enter a keycode, and reads characters from
 * an input filestream (e.g. `stdin`) until an `EOF` or newline is entered.
 * Once an `EOF` or newline is entered, this program ceases blocking and
 * returns, but the entered keycode is stored statically within the module for
 * later processing.
 *
 * This function is used in conjunction with `keyboard_process_keycode`
 * to send the full keycode to the Nexus Keycode library.
 *
 * @param instream the input stream to receive keys from, e.g. `stdin`
*/
void keyboard_prompt_keycode(FILE* instream);

/* @brief Process the keycode previously entered via `keyboard_prompt_keycode`.
 *
 * The Nexus Keycode library allows for keys to be processed either
 * 'all at once' or 'key-by-key'. This function passes the keycode entered in
 * `keyboard_prompt_keycode` to the Nexus Keycode library.
 *
 * There is no return value, as the feedback for each key (and the entire
 * keycode) is returned separately via the feedback functions.
*/
void keyboard_process_keycode(void);

#endif
