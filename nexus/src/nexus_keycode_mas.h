/** \file
 * Nexus Keycode Message Assembly Module (Header)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#ifndef NEXUS__KEYCODE__SRC__NEXUS_KEYCODE_MAS_H_
#define NEXUS__KEYCODE__SRC__NEXUS_KEYCODE_MAS_H_

#include "src/internal_keycode_config.h"

#if NEXUS_KEYCODE_ENABLED

#include <stdbool.h>
#include <stdint.h>

// keycodev1 activation messages are 14 characters long
// smallpadv1 activation messages are 15 characters long
// "Passthrough" keycode messages may be up to 30 characters
// This limit is only used to determine the max 'buffer' for incoming messages
#define NEXUS_KEYCODE_MAX_MESSAGE_LENGTH 30

/** "Frame" of incoming keys to process.
 *
 * Stores incoming key values, as well as the length/number of keys
 * currently in the frame.
 *
 * Internal and separate from `nx_complete_keycode` as this array
 * is a constant length, where `nx_complete_keycode` varies based on
 * the keycode being passed by the product code.
 */
NEXUS_PACKED_STRUCT nexus_keycode_frame
{
    nx_keycode_key keys[NEXUS_KEYCODE_MAX_MESSAGE_LENGTH];
    uint8_t length;
};

/* Returns number of attempts remaining before input is rate limited.
 *
 * Only valid if rate limiting is enabled. If this returns '0', the input
 * is currently rate limited. If this returns a nonzero value, it indicates
 * the number of keycodes that may be entered before rate limiting is
 * engaged.
 * \returns number of keycode input attempts remaining before rate limiting
 */
uint32_t nexus_keycode_rate_limit_attempts_remaining(void);

// message-assembly core
typedef void (*nexus_keycode_mas_message_handler)(
    const struct nexus_keycode_frame*);

void nexus_keycode_mas_init(const nexus_keycode_mas_message_handler handler);
void nexus_keycode_mas_deinit(void);
uint32_t nexus_keycode_mas_process(const uint32_t seconds_elapsed);
void nexus_keycode_mas_reset(void);

// message-assembly bookend scheme
void nexus_keycode_mas_bookend_init(const nx_keycode_key start,
                                    const nx_keycode_key end,
                                    uint8_t stop_length);
void nexus_keycode_mas_bookend_reset(void);

/** Internal functions, not for calls outside this module.
 *
 * The `NEXUS_INTERNAL_IMPL_NON_STATIC` flag exposes these functions in
 * the header if in a unit testing scenario.
 */
#ifdef NEXUS_INTERNAL_IMPL_NON_STATIC

bool nexus_keycode_mas_init_completed(void);

// internal rate-limiting routines
void nexus_keycode_rate_limit_add_time(const uint32_t seconds_elapsed);
bool nexus_keycode_is_rate_limited(void);
void nexus_keycode_rate_limit_deduct_msg(void);

/* Return the number of grace period keycodes remaining.
 *
 * Based on the current rate-limiting bucket value (in seconds), determine
 * the remaining grace period keycodes.  Initially, the rate limiting bucket
 * is set to NEXUS_RL_INITIAL_GRACEPERIOD_KEYCODES * NEXUS_RL_SECS_PER_MSG,
 * but as messages are entered, the rate limiting bucket decrements.
 *
 * This function is used to recalculate the remaining grace period keycodes
 * as keycodes are entered and processed.
 *
 * This function does not assign or modify the internal counter for grace
 * period keycodes, but simply returns the expected number of grace period
 * keycodes remaining based on the current rate limiting bucket.
 *
 * \param cur_rl_bucket_seconds current rate limiting seconds value
 * \return number of grace period keycodes remaining
 */
uint8_t nexus_keycode_mas_remaining_graceperiod_keycodes(
    const uint32_t cur_rl_bucket_seconds);

/* Update the nonvolatile counter of remaining grace period keycodes.
 *
 * Compares the value of `new_graceperiod_keycodes` to the internal counter
 * of grace period keycodes remaining.  If the values are the same, this
 * function returns false.  If the values are different, the internal counter
 * is set to the value `new_graceperiod_keycodes`, and an NV update occurs
 * (block `PAYG_NV_BLOCK_ID_KEYCODE_MAS_STORED`).
 *
 * \param new_graceperiod_keycodes New value of grace period keycodes to set
 * \returns true if an attempt to update NV occurred, false otherwise.
 */
bool nexus_keycode_mas_graceperiod_keycodes_update_nv(
    const uint8_t new_graceperiod_keycodes);

void nexus_keycode_mas_push(const nx_keycode_key key);
void nexus_keycode_mas_finish(void);

uint32_t nexus_keycode_mas_bookend_process(void);
void nexus_keycode_mas_bookend_push(const nx_keycode_key symbol);
#endif

#endif /* if NEXUS_KEYCODE_ENABLED */
#endif /* ifndef NEXUS__KEYCODE__SRC__NEXUS_KEYCODE_MAS_H_ */
