/**
 * @file processing.h
 * @author Angaza
 * @date 25 February 2020
 * @brief File implementing `port_request_processing`.
 *
 * The Nexus Keycode library calls `port_request_processing` to request that
 * `nx_keycode_process` is called by the application during the next idle
 * period. Additionally, whenever `nx_keycode_process` is called, it returns the
 * maximum number of seconds (as a `uint32_t`) that can elapse before the
 * application must call `nx_keycode_process` again.
 *
 * In other words, there are two times when the application must call
 * `nx_keycode_process`:
 *
 * 1) After the library calls `port_request_processing`
 * 2) Before the number of seconds returned by the previous call of
 *    `nx_keycode_process` elapses
 */

#ifndef PROCESSING_H
#define PROCESSING_H

/* @brief Initializes the internal state of the processing module.
 *
 * This function sets up the internal timer and other details allowing
 * `nx_keycode_process` to be called periodically.
 */
void processing_init(void);

/* @brief Called on program exit to delete a timer used in this module.
 *
 * The implementation of this module allocates a timer via `timer_create`,
 * and if the program exits, the timer resource should be freed via
 * `timer_delete`.
 *
 * Call this function when exiting the program to clean up resources.
 */
void processing_deinit(void);

/* @brief Function that calls `nx_keycode_process` if required.
 *
 * This function is intended to be called by the main application during
 * idle periods.
 */
void processing_execute(void);

#endif
