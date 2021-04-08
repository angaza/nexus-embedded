/** \file
 * Nexus Keycode Extended (Implementation)
 * \author Angaza
 * \copyright 2021 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 *  or substantial portions of the Software.
 */

#include "src/nexus_keycode_pro_extended.h"

#if NEXUS_KEYCODE_ENABLED
    #include "include/nxp_keycode.h"
    #include "src/nexus_keycode_pro.h"

    #define NEXUS_KEYCODE_EXTENDED_COMPUTE_BYTES_MAX_SIZE 10

bool nexus_keycode_pro_extended_small_parse(
    struct nexus_bitstream* command_bitstream,
    struct nexus_keycode_pro_extended_small_message* extended_message)
{
    // 26 bits: atttbbbbbbbbbbmmmmmmmmmmmm
    NEXUS_ASSERT(command_bitstream->length == 26,
                 "Smallpad extended command message not 26 bits in length");
    // skip first bit (indicator of whether the passthrough message is an
    // extension keycode or not)
    // Should already be consumed by the upstream caller passing the remaining
    // 25 bits to this function
    NEXUS_ASSERT(
        command_bitstream->position == 1,
        "Unexpected position of incoming passthrough smallpad message");

    const uint8_t type_code = nexus_bitstream_pull_uint8(command_bitstream, 3);

    if (type_code !=
        NEXUS_KEYCODE_PRO_EXTENDED_SMALL_TYPE_SET_CREDIT_AND_WIPE_FLAG)
    {
        // No other types implemented
        return false;
    }
    // we've checked that the uint8_t is castable into the enum already
    extended_message->type_code = (uint8_t) type_code;

    extended_message->body.set_credit_wipe_flag.truncated_message_id =
        nexus_bitstream_pull_uint8(command_bitstream, 2);

    NEXUS_ASSERT(
        command_bitstream->position == 6,
        "Unexpected position after parsing type code and first two body bits");

    extended_message->body.set_credit_wipe_flag.increment_id =
        nexus_bitstream_pull_uint8(command_bitstream, 8);

    extended_message->check =
        nexus_bitstream_pull_uint16_be(command_bitstream, 12);

    NEXUS_ASSERT(command_bitstream->position == command_bitstream->length,
                 "Pulled all bits from smallpad bearer message and have not "
                 "reached length of input bitstream");

    return true;
}

static uint16_t _nexus_keycode_pro_extended_small_auth_arbitrary_bytes(
    const uint8_t* bytes,
    uint8_t bytes_count,
    const struct nx_common_check_key* key)
{
    const struct nexus_check_value check_val =
        nexus_check_compute(key, bytes, bytes_count);

    // obtain upper 12 bits of check
    const uint16_t upper_check =
        (uint16_t)(nexus_check_value_as_uint64(&check_val) >> 52);

    return upper_check;
}

NEXUS_IMPL_STATIC bool
_nexus_keycode_pro_extended_small_message_infer_inner_compute_auth(
    const struct nexus_keycode_pro_extended_small_message* const message,
    const struct nx_common_check_key* const secret_key)
{
    uint8_t compute_bytes[NEXUS_KEYCODE_EXTENDED_COMPUTE_BYTES_MAX_SIZE] = {0};
    bool success = false;
    uint16_t computed_check;

    // first 4 bytes are the command ID for all message types
    const uint32_t inferred_message_id =
        nexus_endian_htole32(message->inferred_message_id);
    memcpy(&compute_bytes, &inferred_message_id, 4);

    NEXUS_ASSERT(sizeof(message->inferred_message_id) == 4,
                 "Invalid command ID size");

    // 5th byte is just the type code
    compute_bytes[4] = (uint8_t) message->type_code;

    uint8_t bytes_count = 5;

    // no other types are currently handled - just set credit + wipe flag
    if (message->type_code ==
        NEXUS_KEYCODE_PRO_EXTENDED_SMALL_TYPE_SET_CREDIT_AND_WIPE_FLAG)
    {
        // 10 bits packed as little-endian at encoder.
        // 8 leftmost bits = increment ID
        // remaining 8 bits = 'truncated message ID' (6 bits are always 0 here)
        compute_bytes[5] = message->body.set_credit_wipe_flag.increment_id;
        compute_bytes[6] =
            message->body.set_credit_wipe_flag.truncated_message_id;

        NEXUS_STATIC_ASSERT(
            sizeof(
                ((struct
                  nexus_keycode_pro_extended_small_message_body_set_credit_wipe_flag*) 0)
                    ->truncated_message_id) == 1,
            "Unexpected struct size for truncated_message_id");
        NEXUS_STATIC_ASSERT(
            sizeof(
                ((struct
                  nexus_keycode_pro_extended_small_message_body_set_credit_wipe_flag*) 0)
                    ->increment_id) == 1,
            "Unexpected struct size for increment_id");

        bytes_count = bytes_count + 2;

        computed_check = _nexus_keycode_pro_extended_small_auth_arbitrary_bytes(
            compute_bytes, bytes_count, secret_key);

        if (computed_check == message->check)
        {
            success = true;
        }
    }
    // sanity check for tests
    NEXUS_ASSERT(bytes_count <= NEXUS_KEYCODE_EXTENDED_COMPUTE_BYTES_MAX_SIZE,
                 "too many bytes to auth!");

    return success;
}

NEXUS_IMPL_STATIC bool
nexus_keycode_pro_extended_small_infer_windowed_message_id(
    struct nexus_keycode_pro_extended_small_message* message,
    const struct nexus_window* window,
    const struct nx_common_check_key* secret_key)
{
    NEXUS_ASSERT(window->center_index - window->flags_below <=
                     window->center_index + window->flags_above,
                 "No IDs to check/validate against");
    bool validated = false;

    // start counting from lowest possible message ID in the window
    message->inferred_message_id = window->center_index - window->flags_below;
    NEXUS_ASSERT(nexus_util_window_id_within_window(
                     window, message->inferred_message_id),
                 "Bottom of window is outside of window - unexpected.");

    if (message->type_code !=
        NEXUS_KEYCODE_PRO_EXTENDED_SMALL_TYPE_SET_CREDIT_AND_WIPE_FLAG)
    {
        // only a single extended message type message ID inference is
        // implemented
        return false;
    }

    // loop through all possible command IDs in the window
    while (nexus_util_window_id_within_window(window,
                                              message->inferred_message_id))
    {
        // only compare IDs where the least significant 2 bits match the
        // received truncated message ID
        if ((message->inferred_message_id & 0x03) ==
            message->body.set_credit_wipe_flag.truncated_message_id)
        {

            // only examine IDs that aren't already set
            // Note: We can't disambiguate easily between 'duplicate' and
            // 'valid' keycodes in this approach, unlike regular set credit
            // keycodes.
            if (!nexus_util_window_id_flag_already_set(
                    window, message->inferred_message_id) &&
                (_nexus_keycode_pro_extended_small_message_infer_inner_compute_auth(
                    message, secret_key)))
            {
                validated = true;
                // don't increment computed command ID any longer
                break;
            }
        }
        message->inferred_message_id++;
    }

    return validated;
}

enum nexus_keycode_pro_response nexus_keycode_pro_extended_small_apply(
    struct nexus_keycode_pro_extended_small_message* extended_message)
{
    struct nexus_window window;
    nexus_keycode_pro_get_current_message_id_window(&window);
    const struct nx_common_check_key secret_key = nxp_keycode_get_secret_key();

    if (!nexus_keycode_pro_extended_small_infer_windowed_message_id(
            extended_message, &window, &secret_key))
    {
        nxp_keycode_feedback_start(NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_INVALID);
        return NEXUS_KEYCODE_PRO_RESPONSE_INVALID;
    }

    // Below this point, we know the command is valid and unused, and
    // apply it.
    // Currently, there is only one keycode command
    // (SET CREDIT + WIPE RESTRICTED FLAG) so we handle it directly here.
    const uint16_t increment_days =
        (uint16_t) nexus_keycode_pro_small_get_set_credit_increment_days(
            (uint8_t) extended_message->body.set_credit_wipe_flag.increment_id);
    NEXUS_ASSERT(increment_days <= 960 ||
                     increment_days == NEXUS_KEYCODE_PRO_SMALL_UNLOCK_INCREMENT,
                 "Unexpected max days exceeded");
    NEXUS_ASSERT(extended_message->inferred_message_id < UINT16_MAX,
                 "Computed message ID is too large");

    if (increment_days != NEXUS_KEYCODE_PRO_SMALL_UNLOCK_INCREMENT)
    {
        nxp_keycode_payg_credit_set(increment_days *
                                    NEXUS_KEYCODE_PRO_SECONDS_IN_DAY);
    }
    else
    {
        nxp_keycode_payg_credit_unlock();
    }

    // set_full_message_id_flag will also update the NV backing the keycode
    // message ID. Set the ID itself and all IDs below it as well
    nexus_keycode_pro_mask_below_message_id(
        (uint16_t) extended_message->inferred_message_id + 1);
    nexus_keycode_pro_reset_custom_flag(NX_KEYCODE_CUSTOM_FLAG_RESTRICTED);

    nxp_keycode_feedback_start(NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_APPLIED);
    return NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED;
}

bool nexus_keycode_pro_extended_small_parse_and_apply_keycode(
    struct nexus_bitstream* passthrough_command_bitstream)
{
    PRINT("Attempting to parse and apply bitstream as extended small protocol "
          "message");

    struct nexus_keycode_pro_extended_small_message message;
    bool parsed = nexus_keycode_pro_extended_small_parse(
        passthrough_command_bitstream, &message);

    if (!parsed)
    {
        PRINT("Failed to parse extended small protocol message");
        nxp_keycode_feedback_start(NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_INVALID);
        return false;
    }

    bool applied = (nexus_keycode_pro_extended_small_apply(&message) ==
                    NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED);

    if (!applied)
    {
        PRINT("Failed to apply extended small protocol message");
        return false;
    }

    // otherwise, applied message
    return true;
}

#endif /* NEXUS_KEYCODE_ENABLED */
