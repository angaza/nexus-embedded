/** \file demo_console.h
 * \brief An interactive console for demonstration purposes
 * \author Angaza
 * \copyright 2021 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 *
 * This module is used to allow entry of various commands (keycodes, requests
 * to 'make GET / POST requests', etc) via UART. This is only used for
 * demonstration/example purposes, and should be removed from a 'real' product.
 */

#ifndef DEMO_CONSOLE__H
#define DEMO_CONSOLE__H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief demo_console_wait_for_user_input
 *
 * Called by `main.c` to block / wait for user input, and
 * take actions based on that user input (on UART). Used for interactive
 * demo.
 *
 * Contains an infinite 'while' loop, and could be run as a separate
 * thread, but currently runs within the main thread.
 *
 * @return void
 */
void demo_console_wait_for_user_input(void);

#ifdef __cplusplus
}
#endif
#endif // DEMO_CONSOLE__H