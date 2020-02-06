/** \file keycode_config.h
 * \brief Manufacturer-specified configuration parameters for Nexus keycode
 * behavior.
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#ifndef __NEXUS__KEYCODE__INC__KEYCODE_CONFIG_H_
#define __NEXUS__KEYCODE__INC__KEYCODE_CONFIG_H_

/** Fixed constant, do not edit */
#define NEXUS_KEYCODE_PROTOCOL_FULL 1
/** Fixed constant, do not edit */
#define NEXUS_KEYCODE_PROTOCOL_SMALL 2

/** Select which version of the Nexus keycode protocol to use.
 *
 * Valid options:
 * - NEXUS_KEYCODE_PROTOCOL_FULL (standard 0-9, *, # keypads)
 * - NEXUS_KEYCODE_PROTOCOL_SMALL (5-character keypads)
 *
 *
 *   Typical keypads for each protocol are below:
 *
 *   <table>
 *   <caption id="full_keypad_table">Full Keypad</caption>
 *   <tr><td>1<td>2<td>3</tr>
 *   <tr><td>4<td>5<td>6</tr>
 *   <tr><td>7<td>8<td>9</tr>
 *   <tr><td>*<td>0<td>#</tr>
 *   </table>
 *
 *   <table>
 *   <caption id="small_keypad_table">Small Keypad</caption>
 *   <tr><td>1<td>2<td>3<td>4<td>5</tr>
 *   </table>
 */
#define NEXUS_KEYCODE_PROTOCOL NEXUS_KEYCODE_PROTOCOL_FULL
//#define NEXUS_KEYCODE_PROTOCOL NEXUS_KEYCODE_PROTOCOL_SMALL

/** Define which physical keys are used on the product.
 *
 * The 'FULL' protocol requires a start key, and end key, and 10 total
 * unique characters for the keycode (usually 0, 1, 2, 3, 4, 5, 6, 7, 8, 9).
 *
 * The "SMALL" protocol requires a start key, has no end key, and requires
 * 4 total unique characters for the keycode (usually 2, 3, 4, and 5).
 *
 * The "SMALL" protocol lacks an end key since all "SMALL" protocol keycodes
 * are the same length (so there is no need for a terminating character). Any
 * character that is not part of the keycode (such as '?') may be used, it is
 * ignored.
 */
#define NEXUS_KEYCODE_UNDEFINED_END_CHAR '?'

#if NEXUS_KEYCODE_PROTOCOL == NEXUS_KEYCODE_PROTOCOL_FULL
#define NEXUS_KEYCODE_START_CHAR '*'
#define NEXUS_KEYCODE_END_CHAR '#'
#define NEXUS_KEYCODE_ALPHABET "0123456789" // excluding start/end
#else
#define NEXUS_KEYCODE_START_CHAR '1'
#define NEXUS_KEYCODE_END_CHAR                                                 \
    NEXUS_KEYCODE_UNDEFINED_END_CHAR // none/undefined for small protocol
#define NEXUS_KEYCODE_ALPHABET "2345" // excluding start/end
#endif

#define NEXUS_KEYCODE_HAS_END_CHAR                                             \
    (NEXUS_KEYCODE_END_CHAR == NEXUS_KEYCODE_UNDEFINED_END_CHAR)

/** Enable Keycode entry rate limiting (Optional)
 *
 * When NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX is nonzero, the
 * Nexus protocol will 'rate limit' the number of keycodes entered per day
 * to prevent brute-force entry attacks. It is recommended to leave this
 * setting enabled (defined) unless the implementing product has a separate
 * method of limiting excessive keycode entry attempts.
 *
 * Rate limiting is performed using a standard Token Bucket algorithm
 * (https://en.wikipedia.org/wiki/Token_bucket), where every keycode entry
 * attempt deducts 1 from the bucket. When the bucket is empty, the keycode
 * entry attempt will always be rejected.
 *
 * NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX: Max number of 'attempts'
 *  at entering a keycode that the bucket can hold. Valid range is 0-255.
 *  If 0, rate limiting is *disabled*.
 *
 * NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT: Keycode
 *  'attempts' in bucket after initial startup. After a power cycle, there
 *  is guaranteed to be *no more than* this many keycode attempts in the
 *  token bucket. Note that the number of attempts remaining in the bucket is
 *  'reduced' after each keycode is entered, and saved to nonvolatile storage
 *  (if at or below NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT).
 *  This value is loaded again on startup, which prevents a user from power
 *  cycling to 'get more' keycode entry attempts.
 *
 * NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT:
 *  One new keycode attempt is added to the bucket every time this amount
 *  of time elapses (until the bucket is full). Valid range is 1-3600.
 *
 *
 * The default parameters will initialize the bucket with 6 attempts,
 * add one attempt to the bucket every 12 minutes (720 seconds), and will
 * stop adding attempts to the bucket once it reaches 128 attempts.
 */

/** Max number of tokens in rate limiting bucket.
 * If this value is 0, 'rate limiting' will be disabled.
 *
 * Valid range: 0-255
 */
#define NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX 128
/** Initial number of tokens in rate limiting bucket.
 * Valid range: 0-255
 */
#define NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT 6
/** Seconds which must elapse to add one token to rate limiting bucket.
 * Valid range: 1-3600
 */
#define NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT 720

/** Enable Keycode entry timeout (Optional)
 *
 * Number of seconds to wait before cancelling a keycode entry operation.
 * This timeout is measured as idle time after *each* key press, not the idle
 * time from the beginning of the keycode.
 *
 * By default, the value of 16 indicates 'the keycode entry will time out if
 * the user does not enter another key for 16 seconds'.
 *
 * To disable this option, set the value to '0'.
 *
 * Valid range: 0-255
 */
#define NEXUS_KEYCODE_PROTOCOL_ENTRY_TIMEOUT_SECONDS 16
//
// The below parameters only apply when using the "FULL" Nexus protocol.
//

/* Factory Quality Control Test Codes
 *
 * These are 'universal' keycodes which have the following functions:
 *
 * FACTORY_QC_SHORT: Adds 10 minutes of credit to any unit
 * FACTORY_QC_LONG: Adds 1 hour of credit to any unit
 *
 * The "LIFETIME_MAX" constants determine how many times over the course
 * of an entire products lifetime these codes may be entered. Once the max
 * is hit, the unit will never accept these codes again (to prevent abuse).
 *
 * For example, if the value is '5', that means that a unit will stop
 * accepting these codes after 5 entries (and will never accept them again).
 *
 * These codes are often useful for factory or warehouse staff to perform
 * spot inspections on a batch of units.
 *
 * Valid value is between 0 and 15.
 *
 * Enter '0' to completely disable these "FACTORY_QC" codes.
 */
/** Total number of "Short" factory QC codes to accept over product lifetime. */
#define NEXUS_KEYCODE_PRO_FACTORY_QC_SHORT_LIFETIME_MAX 5

/** Total number of "Long" factory QC codes to accept over product lifetime. */
#define NEXUS_KEYCODE_PRO_FACTORY_QC_LONG_LIFETIME_MAX 5

#endif
