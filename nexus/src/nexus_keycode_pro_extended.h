/** \file
 * Nexus Keycode Protocol Module Extended (Header)
 * \author Angaza
 * \copyright 2021 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * Functions provide by this module are 'extended' commands that
 * are not part of the core Nexus Keycode protocol, but are extended by
 * embedded these commands within Nexus keycode "Passthrough Commands".
 *
 * The above copyright notice and license shall be included in all copies
 *  or substantial portions of the Software.
 */

#ifndef NEXUS__KEYCODE__SRC__KEYCODE_PRO_EXTENDED_H__
#define NEXUS__KEYCODE__SRC__KEYCODE_PRO_EXTENDED_H__

#include "src/internal_keycode_config.h"

#if NEXUS_KEYCODE_ENABLED
    #include "src/nexus_util.h"

    #define NEXUS_KEYCODE_PRO_EXTENDED_SMALL_PASSTHROUGH_BIT_ID_EXTENDED_KEYCODE \
        1

// Small protocol extended messages borne in the small (non-extended) protocol
// as passthrough commands, and deinterleaved before being passed to this
// extended command handler module.
// Small protocol extended message structure = 25 bits total.
// 3 bits - type codes
// 10 bits - body (contents dependent on type code)
// 12 bits - MAC

/* Type code for extended small protocol messages */
enum nexus_keycode_pro_extended_small_type_code
{
    // update to '0' in a next pass
    NEXUS_KEYCODE_PRO_EXTENDED_SMALL_TYPE_SET_CREDIT_AND_WIPE_FLAG = 0,
    // type codes 1-7 reserved
};

// increment ID extracted from 8 bits (B) SSBBBBBBBB.
struct nexus_keycode_pro_extended_small_message_body_set_credit_wipe_flag
{
    uint8_t truncated_message_id; // 2-bit LSB of 'full' message ID
    uint8_t increment_id; // valid `set_credit` increment ID
};

union nexus_keycode_pro_extended_small_message_body
{
    struct nexus_keycode_pro_extended_small_message_body_set_credit_wipe_flag
        set_credit_wipe_flag;
};

struct nexus_keycode_pro_extended_small_message
{
    uint32_t inferred_message_id; // Expanded message ID (not transmitted)
    uint8_t type_code; // 3 bits (max value 7)
    uint16_t raw_body_bits; // 10 bits stored as uint16_t
    union nexus_keycode_pro_extended_small_message_body body;
    uint16_t check; // 12 MAC/check, 4 padding
};

/* Parse a passthrough bitstream into an extended small protocol message, if
 * possible.
 *
 * If the bitstream contains fewer than 25 bits left before the end, or the
 * bits do not map to a known extended small protocol message format, return
 * false.
 *
 * Otherwise, return true and place the parsed message contents into
 * `extended_message`.
 *
 * \param extended_message parsed extended_message to apply
 * \return true if the message parses successfully, false otherwise.
 */
bool nexus_keycode_pro_extended_small_parse(
    struct nexus_bitstream* command_bitstream,
    struct nexus_keycode_pro_extended_small_message* extended_message);

/* Apply a small protocol 'extended' keycode message.
 *
 * Will trigger keycode feedback and update PAYG credit state, may modify Nexus
 * Keycode related state and NV blocks.
 *
 * \param extended_message parsed extended_message to apply
 * \return keycode feedback to display in response to applying this message
 */
enum nexus_keycode_pro_response nexus_keycode_pro_extended_small_apply(
    struct nexus_keycode_pro_extended_small_message* extended_message);

/* Handle a passthrough command that represents an extended small protocol
 * keycode.
 *
 * Expects the bitstream to be at position 1 (already consumed the 'application
 * ID') and interprets the remaining 25 bits as an extended 'small' keycode.
 *
 * \param bitstream containing the passthrough keycode
 * \return True if keycode valid and applied, false otherwise
 */
bool nexus_keycode_pro_extended_small_parse_and_apply_keycode(
    struct nexus_bitstream* passthrough_command_bitstream);

    #ifdef NEXUS_INTERNAL_IMPL_NON_STATIC
bool nexus_keycode_pro_extended_small_infer_windowed_message_id(
    struct nexus_keycode_pro_extended_small_message* message,
    const struct nexus_window* window,
    const struct nx_common_check_key* secret_key);

bool _nexus_keycode_pro_extended_small_message_infer_inner_compute_auth(
    const struct nexus_keycode_pro_extended_small_message* const message,
    const struct nx_common_check_key* const secret_key);
    #endif // NEXUS_INTERNAL_IMPL_NON_STATIC

#endif /* NEXUS_KEYCODE_ENABLED */
#endif /* ifndef NEXUS__KEYCODE__SRC__KEYCODE_PRO_EXTENDED_H__ */
