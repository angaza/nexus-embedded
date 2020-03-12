/** \file
 * Nexus Keycode Protocol Module (Implementation)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "src/nexus_keycode_pro.h"
#include "include/nxp_core.h"
#include "include/nxp_keycode.h"
#include "src/nexus_keycode_core.h"
#include "src/nexus_keycode_util.h"

//
// NON-ADJUSTABLE PROTOCOL CONSTANTS
//

// Internal identifier used in mapping message ID flags
#define NEXUS_KEYCODE_PRO_MAX_MESSAGE_ID_BYTE 3

//
// Common to both protocol variants
//
const uint32_t NEXUS_KEYCODE_PRO_QC_LONG_TEST_MESSAGE_SECONDS = 3600; // 1 hour

const uint8_t NEXUS_KEYCODE_PRO_UNIVERSAL_SHORT_TEST_SECONDS = 127;

// there is no stop character defined for 'small' protocol, but all valid
// messages are 14 digits in length (after the start character)
#define NEXUS_KEYCODE_MESSAGE_LENGTH_MAX_DIGITS_SMALL 14

// 60 sec/min, 60 min/hr, 24 hr/day
const uint32_t NEXUS_KEYCODE_PRO_SECONDS_IN_HOUR = 60 * 60;
const uint32_t NEXUS_KEYCODE_PRO_SECONDS_IN_DAY = 60 * 60 * 24;

#if NEXUS_KEYCODE_PROTOCOL == NEXUS_KEYCODE_PROTOCOL_SMALL
const uint8_t NEXUS_KEYCODE_PRO_STOP_LENGTH =
    NEXUS_KEYCODE_MESSAGE_LENGTH_MAX_DIGITS_SMALL;
#elif NEXUS_KEYCODE_PROTOCOL == NEXUS_KEYCODE_PROTOCOL_FULL
const uint8_t NEXUS_KEYCODE_PRO_STOP_LENGTH =
    NEXUS_KEYCODE_PROTOCOL_NO_STOP_LENGTH;
#else
#error "Invalid NEXUS_KEYCODE_PROTOCOL defined."
#endif

//
// Small Protocol
//
const uint8_t NEXUS_KEYCODE_PRO_SMALL_MAX_TEST_FUNCTION_ID = 127;
const uint8_t NEXUS_KEYCODE_PRO_SMALL_SET_LOCK_INCREMENT_ID = 254;
const uint8_t NEXUS_KEYCODE_PRO_SMALL_SET_UNLOCK_INCREMENT_ID = 255;
const uint16_t NEXUS_KEYCODE_PRO_SMALL_UNLOCK_INCREMENT = UINT16_MAX;
const uint8_t NEXUS_KEYCODE_PRO_SMALL_ALPHABET_LENGTH = 4;

//
// Full protocol
//
const uint8_t NEXUS_KEYCODE_PRO_FULL_ALPHABET_LENGTH = 10;
const uint32_t NEXUS_KEYCODE_PRO_FULL_UNLOCK_INCREMENT = 99999; // 99999 hours
// Full protocol an additional 'short' variant of QC code
const uint32_t NEXUS_KEYCODE_PRO_QC_SHORT_TEST_MESSAGE_SECONDS = 600;

// 14 total characters in full "Activation" message, 8 non-check characters
const uint8_t NEXUS_KEYCODE_PRO_FULL_CHECK_CHARACTER_COUNT = 6;
const uint8_t NEXUS_KEYCODE_PRO_FULL_DEVICE_ID_MIN_CHARACTER_COUNT = 8;
const uint8_t NEXUS_KEYCODE_PRO_FULL_DEVICE_ID_MAX_CHARACTER_COUNT = 10;

// activation messages are fixed at 14 digits in 'full' protocol
#define NEXUS_KEYCODE_MESSAGE_LENGTH_ACTIVATION_MESSAGE_FULL 14

//
// CORE
//

static struct
{
    struct nexus_keycode_frame frame;
    bool pending;
    nexus_keycode_pro_parse_and_apply parse_and_apply;
} _this_core;

// Protocol-specific parameters (alphabet, etc)
static NEXUS_PACKED_STRUCT protocol
{
    const char* alphabet;
}
_this_protocol;

// RECEIVED MESSAGE ID TRACKING

// Received Message ID Masks - Structure Dependent on Keycode Protocol Used
// flags must be persisted to flash.
static NEXUS_PACKED_STRUCT message_ids
{
    // data that is persisted to flash
    NEXUS_PACKED_STRUCT
    {
        uint8_t received_flags[5];
    }
    flags_0_23; // only flags 0-23 are used currently (2 free bytes)
    NEXUS_PACKED_STRUCT
    {
        uint8_t qc_test_codes_received;
        uint32_t pd_index; // Max message ID received
    }
    code_counts;
    NEXUS_PACKED_STRUCT
    {
        uint8_t pad[2];
    }
    padding;
}
_nexus_keycode_stored;

//
// STATIC SANITY ASSERT
//
NEXUS_STATIC_ASSERT(
    sizeof(_nexus_keycode_stored) ==
        (NX_CORE_NV_BLOCK_1_LENGTH - NEXUS_NV_BLOCK_ID_WIDTH -
         NEXUS_NV_BLOCK_CRC_WIDTH),
    "nexus_keycode_pro: _nexus_keycode_stored invalid size for NV block.");

// FORWARD DECLARATIONS

static void _update_keycode_pro_nv_blocks(void);

#ifndef NEXUS_INTERAL_IMPL_NON_STATIC
NEXUS_IMPL_STATIC bool
nexus_keycode_pro_small_parse(const struct nexus_keycode_frame* frame,
                              struct nexus_keycode_pro_small_message* parsed);
NEXUS_IMPL_STATIC enum nexus_keycode_pro_response nexus_keycode_pro_small_apply(
    const struct nexus_keycode_pro_small_message* message);

NEXUS_IMPL_STATIC uint16_t nexus_keycode_pro_small_compute_check(
    const struct nexus_keycode_pro_small_message* message,
    const struct nx_core_check_key* key);

NEXUS_IMPL_STATIC bool nexus_keycode_pro_full_parse_activation(
    struct nexus_keycode_frame* frame,
    struct nexus_keycode_pro_full_message* parsed);

NEXUS_IMPL_STATIC bool nexus_keycode_pro_full_parse_factory_and_passthrough(
    const struct nexus_keycode_frame* frame,
    struct nexus_keycode_pro_full_message* parsed);

NEXUS_IMPL_STATIC enum nexus_keycode_pro_response nexus_keycode_pro_full_apply(
    const struct nexus_keycode_pro_full_message* message);

NEXUS_IMPL_STATIC enum nexus_keycode_pro_response
nexus_keycode_pro_full_apply_activation(
    const struct nexus_keycode_pro_full_message* message);

NEXUS_IMPL_STATIC enum nexus_keycode_pro_response
nexus_keycode_pro_full_apply_factory(
    const struct nexus_keycode_pro_full_message* message);

NEXUS_IMPL_STATIC void
nexus_keycode_pro_full_deinterleave(struct nexus_keycode_frame* frame,
                                    const uint32_t check_value);

NEXUS_IMPL_STATIC uint32_t nexus_keycode_pro_full_compute_check(
    const struct nexus_keycode_pro_full_message* message,
    const struct nx_core_check_key* key);

NEXUS_IMPL_STATIC bool
nexus_keycode_pro_mask_idx_from_message_id(const uint16_t full_message_id,
                                           uint8_t* mask_id_index);

NEXUS_IMPL_STATIC bool
nexus_keycode_pro_is_message_id_within_window(const uint16_t full_message_id);

NEXUS_IMPL_STATIC bool
nexus_keycode_pro_can_unit_accept_qc_code(const uint32_t qc_credit_seconds);

NEXUS_IMPL_STATIC uint8_t nexus_keycode_pro_get_short_qc_code_count(void);

NEXUS_IMPL_STATIC uint8_t nexus_keycode_pro_get_long_qc_code_count(void);

NEXUS_IMPL_STATIC void
nexus_keycode_pro_increment_short_qc_test_message_count(void);

NEXUS_IMPL_STATIC void
nexus_keycode_pro_increment_long_qc_test_message_count(void);
#endif

// INTERNAL declaration
static uint16_t
nexus_keycode_pro_small_get_add_credit_increment_days(uint8_t increment_id);

// INTERNAL declaration
static uint16_t
nexus_keycode_pro_small_get_set_credit_increment_days(uint8_t increment_id);

void nexus_keycode_pro_init(nexus_keycode_pro_parse_and_apply parse_and_apply,
                            nexus_keycode_pro_protocol_init protocol_init,
                            const char* alphabet)
{
    // initialize core state
    _this_core.pending = false;
    _this_core.parse_and_apply = parse_and_apply;

    // zero out our state
    (void) memset(&_this_protocol, 0x00, sizeof(_this_protocol));
    (void) memset(&_nexus_keycode_stored, 0x00, sizeof(_nexus_keycode_stored));

    // initialize actual protocol state (e.g., full-protocol state)
    (*protocol_init)(alphabet);

    // default value == 23
    nexus_keycode_pro_reset_pd_index();

    // Force a read of the _nexus_keycode_stored nv data
    (void) nexus_keycode_pro_get_full_message_id_flag(0);
}

void nexus_keycode_pro_deinit(void)
{
}

void nexus_keycode_pro_enqueue(const struct nexus_keycode_frame* mas_message)
{
    if (!_this_core.pending)
    {
        _this_core.frame = *mas_message;
        _this_core.pending = true;
    }

    (void) nxp_core_request_processing();
}

uint32_t nexus_keycode_pro_process(void)
{
    // done if no frame is pending
    if (!_this_core.pending)
    {
        return NEXUS_CORE_IDLE_TIME_BETWEEN_PROCESS_CALL_SECONDS;
    }

    // otherwise, interpret the pending frame, then initiate feedback
    enum nexus_keycode_pro_response response =
        (*_this_core.parse_and_apply)(&_this_core.frame);
    enum nxp_keycode_feedback_type feedback = NXP_KEYCODE_FEEDBACK_TYPE_NONE;

    switch (response)
    {
        case NEXUS_KEYCODE_PRO_RESPONSE_INVALID:
            feedback = NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_INVALID;

            break;

        case NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE:
            feedback = NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_VALID;

            break;

        case NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED:
            feedback = NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_APPLIED;

            break;

        case NEXUS_KEYCODE_PRO_RESPONSE_DISPLAY_DEVICE_ID:
            feedback = NXP_KEYCODE_FEEDBACK_TYPE_DISPLAY_SERIAL_ID;

            break;

        case NEXUS_KEYCODE_PRO_RESPONSE_NONE:
            feedback = NXP_KEYCODE_FEEDBACK_TYPE_NONE;

            break;

        default:
            NEXUS_ASSERT(false, "unrecognized protocol response");
    };

    (void) nxp_keycode_feedback_start(feedback);

    _this_core.pending = false;

    // Process at least 2x as often as the default keycode timeout
    return NEXUS_KEYCODE_PROTOCOL_ENTRY_TIMEOUT_SECONDS >> 1;
}

//
// REDUCED-ALPHABET PROTOCOL
//

void nexus_keycode_pro_small_init(const char* alphabet)
{
// assert that the alphabet size is valid for the keycode protocol type
// Note: this will be compiled out, we don't use strlen in production
// code.
#ifdef NEXUS_USE_DEFAULT_ASSERT
    const uint32_t alphabet_len = (uint32_t) strlen(alphabet);
    NEXUS_ASSERT(alphabet_len == NEXUS_KEYCODE_PRO_SMALL_ALPHABET_LENGTH,
                 "unsupported keycode alphabet size");
#endif
    _this_protocol.alphabet = alphabet;
}

// Used as the last-step in parsing.
NEXUS_IMPL_STATIC uint32_t
nexus_keycode_pro_infer_full_message_id(const uint8_t compressed_message_id,
                                        const uint32_t current_pd_index,
                                        const uint8_t valid_id_count_below,
                                        const uint8_t valid_id_count_above)
{
    NEXUS_ASSERT(compressed_message_id <=
                     valid_id_count_above + valid_id_count_below,
                 "Cannot infer message ID; already above mask");
    NEXUS_ASSERT(current_pd_index <= UINT32_MAX - valid_id_count_above,
                 "Pd too large or id count above too large");

    uint32_t cur_id = current_pd_index - valid_id_count_below;

    while (cur_id <= current_pd_index + valid_id_count_above)
    {
        // 6-LSB = 0x3F
        if ((cur_id & 0x3F) == compressed_message_id)
        {
            break;
        }
        cur_id++;
    }
    return cur_id;
}

// Used to update "Pd" (window 'center') value
// Does write NV after updating Pd.
NEXUS_IMPL_STATIC void nexus_keycode_pro_increase_pd_and_shift_window_right(
    const uint32_t pd_increment)
{
    // increased pd by more than lower window size, clear mask.
    // Warning: pd_increment is assumed to be valid (not too large)
    if (pd_increment > NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_BEFORE_PD)
    {
        nexus_keycode_pro_wipe_message_ids_in_window();
    }
    else
    {
        // only use flags 0-23 inclusive.
        uint8_t new_mask[NEXUS_KEYCODE_PRO_MAX_MESSAGE_ID_BYTE] = {0};

        // temporary storage for new mask (starting from 0 offset)
        struct nexus_bitset new_mask_bitset;
        nexus_bitset_init(&new_mask_bitset, &new_mask[0], sizeof(new_mask));

        struct nexus_bitset old_mask_bitset;
        nexus_bitset_init(&old_mask_bitset,
                          &_nexus_keycode_stored.flags_0_23.received_flags[0],
                          sizeof(new_mask));

        /* E.g. Pd=23, pd_increment = 2 (final Pd = 25)
         * Entire window will shift to the right by 2.
         *
         * So, we want to copy all IDs in the lower portion of the window
         * starting at the leftmost position in the current window + the
         * pd_increment.  Everything to the left of this is 'lost' when we
         * move the window, and so won't be in the new mask.
         */
        for (uint32_t i = pd_increment;
             i <= (uint8_t) NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_BEFORE_PD;
             i++)
        {
            // copy values from the old mask into the new mask,
            // offset by pd_inc
            if (nexus_bitset_contains(&old_mask_bitset, (uint16_t) i))
            {
                nexus_bitset_add(&new_mask_bitset,
                                 (uint16_t)(i - pd_increment));
            }
        }
        // copy new mask back over the existing NV flags
        (void) memcpy(&_nexus_keycode_stored.flags_0_23.received_flags,
                      &new_mask,
                      sizeof(new_mask));
    }

    // Update our current Pd after updating the window/mask.
    _nexus_keycode_stored.code_counts.pd_index += pd_increment;
    _update_keycode_pro_nv_blocks();
}

NEXUS_IMPL_STATIC bool
nexus_keycode_pro_small_parse(const struct nexus_keycode_frame* frame,
                              struct nexus_keycode_pro_small_message* message)
{
    // All 'small' protocol messages are the same fixed length, same as the
    // 'max' length. Reject all messages not this length in small protocol
    if (frame->length != NEXUS_KEYCODE_MESSAGE_LENGTH_MAX_DIGITS_SMALL)
    {
        return false;
    }
    // convert keys to bits (28-bit message)
    uint8_t message_bytes[4];
    struct nexus_bitstream message_bitstream;

    nexus_bitstream_init(
        &message_bitstream, message_bytes, sizeof(message_bytes) * 8, 0);

    for (uint8_t i = 0; i < NEXUS_KEYCODE_MESSAGE_LENGTH_MAX_DIGITS_SMALL; ++i)
    {
        const nx_keycode_key key = frame->keys[i];

        for (uint8_t j = 0; _this_protocol.alphabet[j] != '\0'; ++j)
        {
            if (key == _this_protocol.alphabet[j])
            {
                // the alphabet size is assumed to be four; two bits are pushed
                nexus_bitstream_push_uint8(&message_bitstream, j, 2);
                break;
            }
        }

        // was this symbol outside the alphabet?
        if (nexus_bitstream_length_in_bits(&message_bitstream) != (i + 1) * 2)
        {
            // then reject the message
            return false;
        }
    }

#ifdef NEXUS_USE_DEFAULT_ASSERT
    const uint32_t message_bits_length =
        nexus_bitstream_length_in_bits(&message_bitstream);
    NEXUS_ASSERT(message_bits_length == 28,
                 "failed to obtain the expected message length");
#endif

    // pull the check field from the bitstream, first, so that we can
    // deinterleave
    nexus_bitstream_set_bit_position(&message_bitstream,
                                     16); // position of the check bits

    message->check = nexus_bitstream_pull_uint16_be(&message_bitstream, 12);

    // compute pseudorandom bytes for deinterleaving
    uint8_t prng_bytes[sizeof(message_bytes)];

    message->check =
        nexus_endian_htobe16(message->check); // need well-defined layout

    nexus_check_compute_pseudorandom_bytes(&NEXUS_INTEGRITY_CHECK_FIXED_00_KEY,
                                           (const void*) &message->check,
                                           sizeof(message->check),
                                           (void*) prng_bytes,
                                           sizeof(prng_bytes));

    message->check = nexus_endian_be16toh(message->check);
    // extract other message fields, while deinterleaving them using the PRNG
    // output
    struct nexus_bitstream prng_bitstream;

    nexus_bitstream_init(&prng_bitstream,
                         prng_bytes,
                         sizeof(prng_bytes) * 8,
                         sizeof(prng_bytes) * 8);
    nexus_bitstream_set_bit_position(&message_bitstream, 0);

    // only populate the lower 6 bits of the message ID
    const uint8_t received_message_id =
        nexus_bitstream_pull_uint8(&message_bitstream, 6) ^
        nexus_bitstream_pull_uint8(&prng_bitstream, 6);

    message->type_code = nexus_bitstream_pull_uint8(&message_bitstream, 2) ^
                         nexus_bitstream_pull_uint8(&prng_bitstream, 2);

    message->body.activation.increment_id =
        nexus_bitstream_pull_uint8(&message_bitstream, 8) ^
        nexus_bitstream_pull_uint8(&prng_bitstream, 8);

    // Don't infer ID for maintenance/test messages - it is sent as '0'.
    if (message->type_code <
        (uint8_t) NEXUS_KEYCODE_PRO_SMALL_MAINTENANCE_OR_TEST_TYPE)
    {
        // Fill out the remaining 24 bits in the message ID.
        message->full_message_id = nexus_keycode_pro_infer_full_message_id(
            received_message_id,
            _nexus_keycode_stored.code_counts.pd_index,
            NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_BEFORE_PD,
            NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_AFTER_PD);
    }
    else
    {
        message->full_message_id = received_message_id;
    }

    return true;
}

NEXUS_IMPL_STATIC enum nexus_keycode_pro_response nexus_keycode_pro_small_apply(
    const struct nexus_keycode_pro_small_message* message)
{
    // Checks 'is this message valid?'
    const struct nx_core_check_key secret_key = nxp_keycode_get_secret_key();

    uint16_t check_expected;

    // only use default key to check test messages
    if (message->type_code ==
            (uint8_t) NEXUS_KEYCODE_PRO_SMALL_MAINTENANCE_OR_TEST_TYPE &&
        message->body.maintenance_test.function_id <=
            NEXUS_KEYCODE_PRO_SMALL_MAX_TEST_FUNCTION_ID)
    {
        check_expected = nexus_keycode_pro_small_compute_check(
            message, &NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY);
    }
    // activation and maintenance messages
    else
    {
        check_expected =
            nexus_keycode_pro_small_compute_check(message, &secret_key);
    }

    if (message->check != check_expected)
    {
        return NEXUS_KEYCODE_PRO_RESPONSE_INVALID;
    }

    // activation messages, handle message ID properly.
    if (message->type_code <
        (uint8_t) NEXUS_KEYCODE_PRO_SMALL_MAINTENANCE_OR_TEST_TYPE)
    {
        // reject any activation message if its already been applied.
        if (nexus_keycode_pro_get_full_message_id_flag(
                (uint16_t) message->full_message_id))
        {
            return NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE;
        }

        // Set Credit (always apply, even if unit is unlocked)
        if (message->type_code ==
            (uint8_t) NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_SET_CREDIT_TYPE)
        {
            nexus_keycode_pro_mask_below_message_id(
                (uint16_t)(message->full_message_id + 1));

            if (message->body.activation.increment_id ==
                NEXUS_KEYCODE_PRO_SMALL_SET_UNLOCK_INCREMENT_ID)
            {
                // unlock unit
                nxp_keycode_payg_credit_unlock();
            }
            else if (message->body.activation.increment_id ==
                     NEXUS_KEYCODE_PRO_SMALL_SET_LOCK_INCREMENT_ID)
            {
                // disable unit
                nxp_keycode_payg_credit_set(0);
            }
            else
            {
                const uint16_t increment_days =
                    nexus_keycode_pro_small_get_set_credit_increment_days(
                        message->body.activation.increment_id);
                nxp_keycode_payg_credit_set(increment_days *
                                            NEXUS_KEYCODE_PRO_SECONDS_IN_DAY);
            }
        }
        // ADD CREDIT (only remaining type_code)
        else if (message->type_code ==
                 (uint8_t) NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_ADD_CREDIT_TYPE)
        {
            nexus_keycode_pro_set_full_message_id_flag(
                (uint16_t) message->full_message_id);

            if (nxp_core_payg_state_get_current() !=
                NXP_CORE_PAYG_STATE_UNLOCKED)
            {
                const uint16_t increment_days =
                    nexus_keycode_pro_small_get_add_credit_increment_days(
                        message->body.activation.increment_id);

                if (increment_days == NEXUS_KEYCODE_PRO_SMALL_UNLOCK_INCREMENT)
                {
                    nxp_keycode_payg_credit_unlock();
                }
                else
                {
                    nxp_keycode_payg_credit_add(
                        increment_days * NEXUS_KEYCODE_PRO_SECONDS_IN_DAY);
                }
            }
            else
            {
                // Mark the add credit keycode as duplicate if we
                // are already unlocked, since it has not created any
                // 'applied' credit change.
                return NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE;
            }
        }
        else
        {
            return NEXUS_KEYCODE_PRO_RESPONSE_INVALID;
        }
    }

    // Maintenance messages
    else if (message->body.maintenance_test.function_id >
             NEXUS_KEYCODE_PRO_SMALL_MAX_TEST_FUNCTION_ID)
    {
        // lower 7 bits (fxn identifier)
        switch (message->body.maintenance_test.function_id & 0x7F)
        {
            case NEXUS_KEYCODE_PRO_SMALL_WIPE_STATE_TARGET_CREDIT_AND_MASK:
                nexus_keycode_pro_reset_pd_index();
                nexus_keycode_pro_reset_test_code_count();
                nexus_keycode_pro_wipe_message_ids_in_window();
            // intentional fallthrough

            case NEXUS_KEYCODE_PRO_SMALL_WIPE_STATE_TARGET_CREDIT:
                // wipe all state data
                // We explicitly cast to indicate that we mean to treat
                // these enum types as integer values
                nxp_keycode_payg_credit_set(0);
                break;

            case NEXUS_KEYCODE_PRO_SMALL_WIPE_STATE_TARGET_MASK:
                nexus_keycode_pro_reset_pd_index();
                nexus_keycode_pro_reset_test_code_count();
                nexus_keycode_pro_wipe_message_ids_in_window();
                break;

            default: // unsupported function command
                NEXUS_ASSERT(false,
                             "Unsupported MAINTENANCE function id received!");
                return NEXUS_KEYCODE_PRO_RESPONSE_INVALID;
        }
    }

    // Test Messages
    else
    {
        bool test_applied = false;
        uint32_t test_credit_secs = 0;

        switch (message->body.maintenance_test.function_id)
        {
            // 2-minute code only applied if disabled
            case NEXUS_KEYCODE_PRO_SMALL_ENABLE_SHORT_TEST:
                if (nxp_core_payg_state_get_current() ==
                    NXP_CORE_PAYG_STATE_DISABLED)
                {
                    test_applied = true;
                    test_credit_secs =
                        NEXUS_KEYCODE_PRO_UNIVERSAL_SHORT_TEST_SECONDS;
                }
                break;

            // 1-hour QC code is "additive"
            case NEXUS_KEYCODE_PRO_SMALL_ENABLE_QC_TEST:
                if (nexus_keycode_pro_get_long_qc_code_count() <
                        NEXUS_KEYCODE_PRO_FACTORY_QC_LONG_LIFETIME_MAX &&
                    nxp_core_payg_state_get_current() !=
                        NXP_CORE_PAYG_STATE_UNLOCKED)
                {
                    test_applied = true;
                    test_credit_secs =
                        NEXUS_KEYCODE_PRO_QC_LONG_TEST_MESSAGE_SECONDS;
                }
                break;

            default: // unsupported function command
                NEXUS_ASSERT(false, "Unsupported TEST function id received!");
                return NEXUS_KEYCODE_PRO_RESPONSE_INVALID;
        }

        if (test_applied)
        {
            nxp_keycode_payg_credit_add(test_credit_secs);
            // increment the count of received codes
            if (message->body.maintenance_test.function_id ==
                (uint8_t) NEXUS_KEYCODE_PRO_SMALL_ENABLE_QC_TEST)
            {
                nexus_keycode_pro_increment_long_qc_test_message_count();
            }
        }
        else
        {
            return NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE;
        }
    }

    // success unless explicit failure
    return NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED;
}

enum nexus_keycode_pro_response
nexus_keycode_pro_small_parse_and_apply(const struct nexus_keycode_frame* frame)
{
    struct nexus_keycode_pro_small_message pro_message = {0};
    const bool parsed = nexus_keycode_pro_small_parse(frame, &pro_message);

    if (parsed)
    {
        return nexus_keycode_pro_small_apply(&pro_message);
    }
    else
    {
        return NEXUS_KEYCODE_PRO_RESPONSE_INVALID;
    }
}

NEXUS_IMPL_STATIC uint16_t nexus_keycode_pro_small_compute_check(
    const struct nexus_keycode_pro_small_message* message,
    const struct nx_core_check_key* key)
{
    struct nexus_keycode_pro_small_message message_copy = *message;

    // Compute over 6 bytes (4=message_id, 1=type_code, 1=body)
    const struct nexus_check_value value = nexus_check_compute(
        key, &message_copy, sizeof(*message) - sizeof(message->check));

    // use the 12 MSBs of the 64-bit hash as our check value; note that the
    // hash bytes are packed little-endian
    return (uint16_t)(((uint16_t) value.bytes[7] << 4) | (value.bytes[6] >> 4));
}

uint16_t
nexus_keycode_pro_small_get_add_credit_increment_days(uint8_t increment_id)
{
    if (increment_id == 255)
    {
        // preserved for backwards compatibility
        return NEXUS_KEYCODE_PRO_SMALL_UNLOCK_INCREMENT;
    }
    else if (increment_id < 180)
    {
        return (uint16_t)(increment_id + 1); // 1-180 days
    }
    return (uint16_t)((increment_id - 179) * 3 + 180); // 183-405 days
}

uint16_t
nexus_keycode_pro_small_get_set_credit_increment_days(uint8_t increment_id)
{
    if (increment_id < 90)
    {
        return (uint16_t)(increment_id + 1); // 1-90 days
    }
    else if (increment_id < 135)
    {
        return (uint16_t)((increment_id - 89) * 2 + 90); // 92-180 days
    }
    else if (increment_id < 180)
    {
        return (uint16_t)((increment_id - 134) * 4 + 180); // 184-360 days
    }
    else if (increment_id < 225)
    {
        return (uint16_t)((increment_id - 179) * 8 + 360); // 368-720 days
    }
    return (uint16_t)((increment_id - 224) * 16 + 720); // 736-1216 days
}

/** Mathematical mod 10.
 */
static uint8_t _mathmod10(int x)
{
    while (x < 0)
    {
        x += 10;
    }

    return (uint8_t)(((uint32_t) x) % 10);
}

void nexus_keycode_pro_full_init(const char* alphabet)
{
#ifdef NEXUS_USE_DEFAULT_ASSERT
    // assert that the alphabet size is valid for the keycode protocol type
    const uint32_t alphabet_len = (uint32_t) strlen(alphabet);
    NEXUS_ASSERT(alphabet_len == NEXUS_KEYCODE_PRO_FULL_ALPHABET_LENGTH,
                 "unsupported keycode alphabet size");
#endif
    _this_protocol.alphabet = alphabet;
}

/** Parse and apply a keycode message.
 */
enum nexus_keycode_pro_response nexus_keycode_pro_full_parse_and_apply(
    const struct nexus_keycode_frame* raw_frame)
{
    // normalize and parse the message
    struct nexus_keycode_frame frame = *raw_frame;
    struct nexus_keycode_pro_full_message message;

    const bool parsed = nexus_keycode_pro_full_parse(&frame, &message);

    if (!parsed)
    {
        return NEXUS_KEYCODE_PRO_RESPONSE_INVALID;
    }
    else if (message.type_code ==
             (uint8_t) NEXUS_KEYCODE_PRO_FULL_PASSTHROUGH_COMMAND)
    {
        // short circuit - don't apply these messages, don't create feedback.
        return NEXUS_KEYCODE_PRO_RESPONSE_NONE;
    }

    // apply the message
    return nexus_keycode_pro_full_apply(&message);
}

bool nexus_keycode_pro_full_parse(struct nexus_keycode_frame* frame,
                                  struct nexus_keycode_pro_full_message* parsed)
{
    (void) memset(parsed, 0x00, sizeof(struct nexus_keycode_pro_full_message));

    // assume length-14 messages are activation; shorter is factory or
    // passthrough
    // command
    const bool success =
        frame->length == NEXUS_KEYCODE_MESSAGE_LENGTH_ACTIVATION_MESSAGE_FULL ?
            nexus_keycode_pro_full_parse_activation(frame, parsed) :
            nexus_keycode_pro_full_parse_factory_and_passthrough(frame, parsed);

    // parsed message contains the full message ID at this point.
    return success;
}

/* Extract the check digits from a frame, regardless of type, and
 * return the uint32_t value of those check digits.
 */
NEXUS_IMPL_STATIC uint32_t nexus_keycode_pro_full_check_field_from_frame(
    const struct nexus_keycode_frame* frame)
{
    struct nexus_digits digits;
    char digit_chars[NEXUS_KEYCODE_MESSAGE_LENGTH_MAX_DIGITS_FULL];
    NEXUS_ASSERT(frame->length <= NEXUS_KEYCODE_MESSAGE_LENGTH_MAX_DIGITS_FULL,
                 "Frame does not contain a valid keycode.");
    memcpy(&digit_chars, frame->keys, frame->length);
    nexus_digits_init(&digits, digit_chars, frame->length);

    const int8_t non_check_char_count =
        (int8_t)(frame->length - NEXUS_KEYCODE_PRO_FULL_CHECK_CHARACTER_COUNT);

    if (non_check_char_count < 0)
    {
        // return 0; which will be essentially an 'invalid' check
        return 0;
    }
    else
    {
        // skim through and ignore the non-check digits
        for (uint8_t i = 0; i < non_check_char_count; ++i)
        {
            (void) nexus_digits_pull_uint8(&digits, 1);
        }
    }

    // extract the 6-digit MAC
    const uint32_t check_as_int = nexus_digits_pull_uint32(
        &digits, NEXUS_KEYCODE_PRO_FULL_CHECK_CHARACTER_COUNT);

    return check_as_int;
}

/** Parse an activation message packed in a *normalized* frame.
 */
NEXUS_IMPL_STATIC bool nexus_keycode_pro_full_parse_activation(
    struct nexus_keycode_frame* frame,
    struct nexus_keycode_pro_full_message* parsed)
{
    // it's an activation message
    NEXUS_ASSERT(frame->length ==
                     NEXUS_KEYCODE_MESSAGE_LENGTH_ACTIVATION_MESSAGE_FULL,
                 "unsupported activation-message frame length");

    // effectively 'pulls' the last 6 digits of the frame as check/MAC field
    // does not modify frame.
    parsed->check = nexus_keycode_pro_full_check_field_from_frame(frame);

    // activation messages must be deinterleaved
    // (note that we're changing the caller's message frame here!)
    nexus_keycode_pro_full_deinterleave(frame, parsed->check);

    // prepare to access the *de-interleaved* frame as a digit stream
    struct nexus_digits digits;
    char digit_chars[NEXUS_KEYCODE_MESSAGE_LENGTH_ACTIVATION_MESSAGE_FULL];
    memcpy(&digit_chars, frame->keys, frame->length);
    nexus_digits_init(&digits, digit_chars, frame->length);

    // per the protocol spec, *compressed* activation messages have the
    // following structure:
    //
    // * 3-digit header
    // * 5-digit body
    // * 6-digit MAC
    //
    // the 4-digit message header has the following structure:
    //
    // * 1-digit message type
    // * 2-digit message id
    //

    parsed->type_code = nexus_digits_pull_uint8(&digits, 1);

    // extract the 2-digit *compressed* message id
    const uint8_t received_message_id = nexus_digits_pull_uint8(&digits, 2);

    if (received_message_id > NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_AFTER_PD +
                                  NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_BEFORE_PD)
    {
        // Invalid ID larger than window size; don't proceed.
        return false;
    }

    // 'activation' message ID is used during application of message, not check
    parsed->full_message_id = nexus_keycode_pro_infer_full_message_id(
        received_message_id,
        _nexus_keycode_stored.code_counts.pd_index,
        NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_BEFORE_PD,
        NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_AFTER_PD);

    // extract the 8-digit body
    switch (parsed->type_code)
    {
        case NEXUS_KEYCODE_PRO_FULL_ACTIVATION_ADD_CREDIT:
        // intentional fallthrough
        case NEXUS_KEYCODE_PRO_FULL_ACTIVATION_DEMO_CODE:
        // intentional fallthrough
        case NEXUS_KEYCODE_PRO_FULL_ACTIVATION_SET_CREDIT:
            // ADD/SET_CREDIT messages have the following body structure:
            //
            // * Hours [5 digits]
            //
            // so, start by extracting the 5-digit hours count
            parsed->body.add_set_credit.hours =
                nexus_digits_pull_uint32(&digits, 5);

            break;

        case NEXUS_KEYCODE_PRO_FULL_ACTIVATION_WIPE_STATE:
            // WIPE_STATE messages have the following body structure:
            //
            // * Reserved [4 digits]
            // * Target Flags [1 digits]
            //
            // so, start by discarding the reserved digits and
            // extracting the 1-digit target number
            (void) nexus_digits_pull_uint32(&digits, 4);
            parsed->body.wipe_state.target =
                nexus_digits_pull_uint8(&digits, 1);

            break;

        default:
            NEXUS_ASSERT(false, "Unsupported ACTIVATION type received!");
            // unrecognized message type; not a valid message
            return false;
    }

    // 'consume' / throw away the 6 check digits at the end of the frame
    (void) nexus_digits_pull_uint32(
        &digits, NEXUS_KEYCODE_PRO_FULL_CHECK_CHARACTER_COUNT);

    // we should now have consumed every digit
    return nexus_digits_length_in_digits(&digits) ==
           nexus_digits_position(&digits);
}

NEXUS_IMPL_STATIC bool nexus_keycode_pro_full_parse_factory_and_passthrough(
    const struct nexus_keycode_frame* frame,
    struct nexus_keycode_pro_full_message* parsed)
{
    // prepare to access the message as a digit stream
    struct nexus_digits digits;
    char digit_chars[NEXUS_KEYCODE_MESSAGE_LENGTH_MAX_DIGITS_FULL];

    if (frame->length > NEXUS_KEYCODE_MESSAGE_LENGTH_MAX_DIGITS_FULL)
    {
        return false;
    }

    memcpy(&digit_chars, frame->keys, frame->length);
    nexus_digits_init(&digits, digit_chars, frame->length);

    // per the protocol spec, factory messages have the following structure:
    //
    // * 1-digit header
    // * N-digit body
    // * 6-digit MAC
    //
    // the factory message header has the following structure:
    //
    // * 1-digit message type
    //
    // so, start by extracting the 1-digit message type
    bool underrun = false;

    const uint32_t type_code_u32 =
        nexus_digits_try_pull_uint32(&digits, 1, &underrun);

    NEXUS_ASSERT(type_code_u32 <= UINT8_MAX, "Invalid type code digit!");
    parsed->type_code = (uint8_t) type_code_u32;

    /* Only supported factory messages are ALLOW_TEST, QC_TEST,
     DEVICE_ID_DISPLAY, and NOMAC_DEVICE_ID_CONFIRMATION */

    NEXUS_ASSERT(parsed->type_code <=
                     (uint8_t) NEXUS_KEYCODE_PRO_FULL_PASSTHROUGH_COMMAND,
                 "Invalid message type!");
    if (parsed->type_code <
        (uint8_t) NEXUS_KEYCODE_PRO_FULL_FACTORY_NOMAC_DEVICE_ID_CONFIRMATION)
    {
        if (parsed->type_code ==
            (uint8_t) NEXUS_KEYCODE_PRO_FULL_FACTORY_QC_TEST)
        {
            // QC TEST codes have the following body structure:
            // * Reserved [3 digits]
            // * QC Variant [2 digits]
            // (3 reserved digits provide flexibility to allow for future test
            // keycodes without additional changes to QC code and maintaining
            // existing message type code structure.)
            // Discarding the reserved digits and extracting the 2-digit
            // variant number:
            (void) nexus_digits_pull_uint32(&digits, 3);
            parsed->body.qc_variant.minutes =
                nexus_digits_pull_uint8(&digits, 2);
        }
        // extract the 6-digit MAC
        parsed->check = nexus_digits_try_pull_uint32(
            &digits, NEXUS_KEYCODE_PRO_FULL_CHECK_CHARACTER_COUNT, &underrun);
    }
    else if (parsed->type_code ==
             (uint8_t)
                 NEXUS_KEYCODE_PRO_FULL_FACTORY_NOMAC_DEVICE_ID_CONFIRMATION)
    {
        /* this path is for the NOMAC Device ID confirmation keycode & extracts
           the 8 to 10-digit Device ID */

        // Message body consists of the Device ID being confirmed.
        // The message frame consists of the body and the single character
        // message ID.
        // To find length of the Device ID confirmed, the frame length is pulled
        // and one is subtracted.
        // Device ID length used in nexus_digits_try_pull_uint32 to guarantee
        // the
        // function includes all digits in the keycode when setting
        // body.nexus_device_id.device_id
        uint8_t serial_id_length = (uint8_t)(frame->length - 1);
        // Ensures serial_id_length is greater than
        // NEXUS_KEYCODE_PRO_FULL_DEVICE_ID_MIN_CHARACTER_COUNT and less than
        // NEXUS_KEYCODE_PRO_FULL_DEVICE_ID_MAX_CHARACTER_COUNT.
        if (serial_id_length <
                NEXUS_KEYCODE_PRO_FULL_DEVICE_ID_MIN_CHARACTER_COUNT ||
            serial_id_length >
                NEXUS_KEYCODE_PRO_FULL_DEVICE_ID_MAX_CHARACTER_COUNT)
        {
            return false;
        }
        // If the Device ID entered is 10 digits and has a value above the
        // maximum uint32_t value, this function will produce an invalid value,
        // which will not match the internal device ID, and will produce
        // appropriate feedback (not 'matching').
        parsed->body.nexus_device_id.device_id =
            nexus_digits_try_pull_uint32(&digits, serial_id_length, &underrun);
    }
    else if (parsed->type_code ==
             (uint8_t) NEXUS_KEYCODE_PRO_FULL_PASSTHROUGH_COMMAND)
    {
        bool command_valid = false;
        // Passthrough commands must consist of at least three digits to be
        // valid.
        // This is because the first digit ('8') is fixed to identify it as
        // a passthrough command, the next digit is a 'subtype ID' identifying
        // the type of passthrough data, and the following digits are the
        // passthrough data body.
        if (digits.length > 2 &&
            digits.length !=
                NEXUS_KEYCODE_MESSAGE_LENGTH_ACTIVATION_MESSAGE_FULL &&
            digits.length < NEXUS_KEYCODE_MESSAGE_LENGTH_MAX_DIGITS_FULL)
        {
            NEXUS_ASSERT(
                digits.position == 1,
                "More than one digit pulled from Passthrough Command message.");
            // Pass the body digits, skipping the type_code digit
            struct nx_keycode_complete_code passthrough_code = {0};
            passthrough_code.keys =
                (nx_keycode_key*) &digits.chars[digits.position];
            passthrough_code.length =
                (uint8_t)(digits.length - digits.position);

            const enum nxp_keycode_passthrough_error result =
                nxp_keycode_passthrough_keycode(&passthrough_code);

            if (result == NXP_KEYCODE_PASSTHROUGH_ERROR_NONE)
            {
                command_valid = true;
            }
        }
        // all nexus_digits aren't consumed in the usual fashion, instead, we
        // pass the raw ASCII digits to the product code.
        return command_valid;
    }

    // we should now have consumed exactly every digit
    return !underrun &&
           nexus_digits_length_in_digits(&digits) ==
               nexus_digits_position(&digits);
}

NEXUS_IMPL_STATIC enum nexus_keycode_pro_response nexus_keycode_pro_full_apply(
    const struct nexus_keycode_pro_full_message* message)
{
    // validate the message
    const struct nx_core_check_key secret_key =
        message->type_code <
                (uint8_t) NEXUS_KEYCODE_PRO_FULL_FACTORY_ALLOW_TEST ?
            nxp_keycode_get_secret_key() :
            NEXUS_INTEGRITY_CHECK_FIXED_00_KEY;

    // check computed against parsed message, not frame
    const uint32_t check_expected =
        nexus_keycode_pro_full_compute_check(message, &secret_key);

    if (message->check != check_expected &&
        message->type_code <
            (uint8_t)
                NEXUS_KEYCODE_PRO_FULL_FACTORY_NOMAC_DEVICE_ID_CONFIRMATION)
    {
        return NEXUS_KEYCODE_PRO_RESPONSE_INVALID;
    }

    // apply the validated message
    enum nexus_keycode_pro_response response;
    if (message->type_code <
        (uint8_t) NEXUS_KEYCODE_PRO_FULL_FACTORY_ALLOW_TEST)
    {
        response = nexus_keycode_pro_full_apply_activation(message);
    }
    else
    {
        response = nexus_keycode_pro_full_apply_factory(message);
    }

    return response;
}

NEXUS_IMPL_STATIC enum nexus_keycode_pro_response
nexus_keycode_pro_full_apply_activation(
    const struct nexus_keycode_pro_full_message* message)
{
// certain version of clang misinterpret this section
#ifndef __clang_analyzer__
    // reject any activation message if its already been applied.
    if (nexus_keycode_pro_get_full_message_id_flag(
            (uint16_t) message->full_message_id))
    {
        return NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE;
    }
#endif

    /* Ignored by WIPE_STATE; but reduce number of comparisons by not
     * explicitly handling that state here.
     */
    const uint32_t credit_increment_seconds =
        message->body.add_set_credit.hours * NEXUS_KEYCODE_PRO_SECONDS_IN_HOUR;

    // apply the message according to its specific semantics
    switch (message->type_code)
    {
        case NEXUS_KEYCODE_PRO_FULL_ACTIVATION_ADD_CREDIT:
            // set only this message ID
            nexus_keycode_pro_set_full_message_id_flag(
                (uint16_t) message->full_message_id);

            if (nxp_core_payg_state_get_current() !=
                NXP_CORE_PAYG_STATE_UNLOCKED)
            {
                nxp_keycode_payg_credit_add(credit_increment_seconds);
            }
            else
            {
                // already unlocked? return duplicate feedback
                return NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE;
            }
            break;

        case NEXUS_KEYCODE_PRO_FULL_ACTIVATION_DEMO_CODE:
            /* Intended for specially designated 'demo' units
             * Note: Demo codes *can* be reused (no message ID is set)
             */
            if (nxp_core_payg_state_get_current() !=
                NXP_CORE_PAYG_STATE_UNLOCKED)
            {
                // The body of the demo code overrides 'hours' to convey
                // 'minutes', so we only need to multiply by 60 here to get
                // the conveyed amount.
                const uint32_t credit_increment_seconds =
                    message->body.add_set_credit.hours * 60;

                nxp_keycode_payg_credit_add(credit_increment_seconds);
            }
            break;

        case NEXUS_KEYCODE_PRO_FULL_ACTIVATION_SET_CREDIT:
            // Invalidate receipt of any messages <= this message ID
            nexus_keycode_pro_mask_below_message_id(
                (uint16_t)(message->full_message_id + 1));

            // unlock the unit
            if (message->body.add_set_credit.hours ==
                NEXUS_KEYCODE_PRO_FULL_UNLOCK_INCREMENT)
            {
                nxp_keycode_payg_credit_unlock();
            }
            else
            {
                nxp_keycode_payg_credit_set(credit_increment_seconds);
            }
            break;

        case NEXUS_KEYCODE_PRO_FULL_ACTIVATION_WIPE_STATE:
            // Invalidate receipt of any messages <= this message ID
            nexus_keycode_pro_mask_below_message_id(
                (uint16_t)(message->full_message_id + 1));

            switch (message->body.wipe_state.target)
            {
                case NEXUS_KEYCODE_PRO_FULL_WIPE_STATE_TARGET_CREDIT_AND_MASK:
                    nexus_keycode_pro_reset_pd_index();
                    nexus_keycode_pro_reset_test_code_count();
                    nexus_keycode_pro_wipe_message_ids_in_window();
                // intentional fallthrough

                case NEXUS_KEYCODE_PRO_FULL_WIPE_STATE_TARGET_CREDIT:
                    // wipe all state data
                    // We explicitly cast to indicate that we mean to treat
                    // these enum types as integer values
                    nxp_keycode_payg_credit_set(0);
                    break;

                case NEXUS_KEYCODE_PRO_FULL_WIPE_STATE_TARGET_MASK_ONLY:
                    nexus_keycode_pro_reset_pd_index();
                    nexus_keycode_pro_reset_test_code_count();
                    nexus_keycode_pro_wipe_message_ids_in_window();
                    break;

                default:
                    NEXUS_ASSERT(false, "Invalid wipe state flag received!");
                    return NEXUS_KEYCODE_PRO_RESPONSE_INVALID;
            }
            break;

        default:
            NEXUS_ASSERT(false, "Invalid activation message type received!");
            return NEXUS_KEYCODE_PRO_RESPONSE_INVALID;
    }
    return NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED;
}

/*@ requires \valid(message);
    assigns \nothing;

    behavior qc_codes_above_limit:
        assumes _nexus_keycode_stored.code_counts.qc_test_codes_received >
            10;
        assumes message->type_code == NEXUS_KEYCODE_PRO_FULL_FACTORY_ALLOW_TEST;
        ensures \result == NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE;
*/
NEXUS_IMPL_STATIC enum nexus_keycode_pro_response
nexus_keycode_pro_full_apply_factory(
    const struct nexus_keycode_pro_full_message* message)
{
    bool test_applied = false;

    // no body included in an 'allow_test' factory message
    switch (message->type_code)
    {
        case NEXUS_KEYCODE_PRO_FULL_FACTORY_ALLOW_TEST:
            // only apply if we are disabled and haven't hit the limit.
            if (nxp_core_payg_state_get_current() ==
                NXP_CORE_PAYG_STATE_DISABLED)
            {
                test_applied = true;
                nxp_keycode_payg_credit_add(
                    NEXUS_KEYCODE_PRO_UNIVERSAL_SHORT_TEST_SECONDS);
            }
            break;

        case NEXUS_KEYCODE_PRO_FULL_FACTORY_QC_TEST:
        {
            const uint32_t qc_credit_seconds =
                message->body.qc_variant.minutes * 60;
            test_applied =
                nexus_keycode_pro_can_unit_accept_qc_code(qc_credit_seconds);
            if (test_applied)
            {
                nxp_keycode_payg_credit_add(qc_credit_seconds);
                if (qc_credit_seconds <=
                    NEXUS_KEYCODE_PRO_QC_SHORT_TEST_MESSAGE_SECONDS)
                {
                    nexus_keycode_pro_increment_short_qc_test_message_count();
                }
                else
                {
                    nexus_keycode_pro_increment_long_qc_test_message_count();
                }
            }
            break;
        }

        case NEXUS_KEYCODE_PRO_FULL_FACTORY_DEVICE_ID_DISPLAY:
            // No credit or state change occurs as a result of this message.
            break;

        case NEXUS_KEYCODE_PRO_FULL_FACTORY_NOMAC_DEVICE_ID_CONFIRMATION:
            if (message->body.nexus_device_id.device_id ==
                nxp_keycode_get_user_facing_id())
            {
                // Signal 'applied' if the ID matches, 'invalid' if not.
                test_applied = true;
            }
            break;

        case NEXUS_KEYCODE_PRO_FULL_PASSTHROUGH_COMMAND:
        // Should not reach here
        // intentional fallthrough

        default:
            NEXUS_ASSERT(false, "should not be reached");
            return NEXUS_KEYCODE_PRO_RESPONSE_INVALID;
    }

    if (test_applied)
    {
        return NEXUS_KEYCODE_PRO_RESPONSE_VALID_APPLIED;
    }
    else if (message->type_code ==
             (uint8_t) NEXUS_KEYCODE_PRO_FULL_FACTORY_DEVICE_ID_DISPLAY)
    {
        return NEXUS_KEYCODE_PRO_RESPONSE_DISPLAY_DEVICE_ID;
    }
    // if the above two are untrue, return the duplicate feedback response
    else
    {
        return NEXUS_KEYCODE_PRO_RESPONSE_VALID_DUPLICATE;
    }
}

// Note: Only "Activation" messages are interleaved.
NEXUS_IMPL_STATIC void
nexus_keycode_pro_full_deinterleave(struct nexus_keycode_frame* frame,
                                    const uint32_t check_value)
{
    // compute pseudorandom bytes for deinterleaving
    uint8_t prng_bytes[NEXUS_KEYCODE_PRO_FULL_ACTIVATION_BODY_CHARACTER_COUNT];

    // only activation messages are de-interleavable.
    NEXUS_ASSERT(frame->length ==
                     NEXUS_KEYCODE_PRO_FULL_ACTIVATION_BODY_CHARACTER_COUNT +
                         NEXUS_KEYCODE_PRO_FULL_CHECK_CHARACTER_COUNT,
                 "frame to deinterleave has wrong length");

    nexus_check_compute_pseudorandom_bytes(&NEXUS_INTEGRITY_CHECK_FIXED_00_KEY,
                                           (const void*) &check_value,
                                           sizeof(check_value),
                                           (void*) prng_bytes,
                                           (uint16_t) sizeof(prng_bytes));

    for (uint8_t i = 0;
         i < NEXUS_KEYCODE_PRO_FULL_ACTIVATION_BODY_CHARACTER_COUNT;
         ++i)
    {
        const nx_keycode_key body_char = frame->keys[i];

        NEXUS_ASSERT('0' <= body_char && body_char <= '9',
                     "body key character not a digit");

        // only deinterleave; always subtract perturbation value
        const uint8_t perturbation = prng_bytes[i];
        const uint8_t body_digit = (uint8_t)(body_char - '0');
        const uint8_t out_digit = _mathmod10(body_digit - perturbation);

        frame->keys[i] = (nx_keycode_key)(out_digit + '0');
    }
}

/**
 * \param message full protocol message to for computing MAC
 * \param key nx_check_key to use in MAC computation
 * \return uint32_t value of check field computed from activation message
 */
NEXUS_IMPL_STATIC uint32_t nexus_keycode_pro_full_compute_check(
    const struct nexus_keycode_pro_full_message* message,
    const struct nx_core_check_key* key)
{
    // Compute over 9 bytes:
    // 4 = full_message_id (as uint32_t)
    // 1 = type_id (uint8_t)
    // 4 = contents of body (full message body always 4 bytes))
    const struct nexus_check_value check_val = nexus_check_compute(
        key,
        message,
        // assumes message.check is uint32_t
        sizeof(struct nexus_keycode_pro_full_message) -
            sizeof(((struct nexus_keycode_pro_full_message*) 0)->check));

    // obtain lower 32 bits of check
    const uint32_t lower_check =
        nexus_check_value_as_uint64(&check_val) & 0xffffffff;

    // obtain the 'decimal representation' of the lowest 6 decimal digits
    // of the check.  Note that leading zeros are *ignored* as the check is now
    // computed over the numeric value represented by the 6 decimal check
    // digits, not the individual digits themselves.
    return lower_check % 1000000;
}

uint32_t nexus_keycode_pro_get_current_pd_index(void)
{
    return _nexus_keycode_stored.code_counts.pd_index;
}

NEXUS_IMPL_STATIC bool
nexus_keycode_pro_is_message_id_within_window(const uint16_t full_message_id)
{
    const uint32_t cur_pd = nexus_keycode_pro_get_current_pd_index();

    const bool in_window =
        ((cur_pd - NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_BEFORE_PD <=
          full_message_id) &&
         (cur_pd + NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_AFTER_PD >=
          full_message_id));

    return in_window;
}

NEXUS_IMPL_STATIC bool
nexus_keycode_pro_mask_idx_from_message_id(const uint16_t full_message_id,
                                           uint8_t* mask_id_index)
{
    // if a message ID is outside the window; we know nothing about it.
    // We assume it is 'not set'.
    if (!nexus_keycode_pro_is_message_id_within_window(full_message_id))
    {
        return false;
    }
    // otherwise, value is in the current window.
    else
    {
        const uint32_t cur_pd = nexus_keycode_pro_get_current_pd_index();

        if (cur_pd >= full_message_id)
        {
            *mask_id_index =
                (uint8_t)(NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_BEFORE_PD -
                          (cur_pd - full_message_id));
        }
        else
        {
            *mask_id_index =
                (uint8_t)(NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_BEFORE_PD +
                          (full_message_id - cur_pd));
        }

        return true;
    }
}

// If in the future any other method updates PAYG credit in a manner that
// should 'invalidate' certain message IDs (preventing previously generated
// keycodes from being entered), this function should be called to update
// the window and mask as well.
NEXUS_IMPL_STATIC bool nexus_keycode_pro_update_window_and_message_mask_id(
    const uint16_t full_message_id, uint8_t* mask_id_index)
{
    const uint32_t cur_pd = nexus_keycode_pro_get_current_pd_index();
    bool pd_increased = false;

    /* RECEIVE_WINDOW_BEFORE_PD is also the index value of 'Pd' in the window.
     * if full_message-id > cur_pd; we mask everything below cur_pd
     * (e.g. mask_id_index = NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_BEFORE_PD).
     *
     * If full_message_id <= cur_pd; mask_id_index reduced by the difference
     * between the current Pd and the incoming message ID.
     */
    *mask_id_index = NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_BEFORE_PD;

    // move window to the right
    if (full_message_id > cur_pd)
    {
        const uint32_t pd_increment = full_message_id - cur_pd;
        pd_increased = true;

        nexus_keycode_pro_increase_pd_and_shift_window_right(pd_increment);
    }
    // full message is below PD but in the window; return its index.
    else if (full_message_id >=
             cur_pd - NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_BEFORE_PD)
    {
        // assumes mask is in window
        *mask_id_index = (uint8_t)(NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_BEFORE_PD -
                                   (cur_pd - full_message_id));

        NEXUS_ASSERT(*mask_id_index <=
                         NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_BEFORE_PD,
                     "calculated keycode mask index too large");

        // only allow modification to sane values
        if (*mask_id_index > NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_BEFORE_PD)
        {
            *mask_id_index = NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_BEFORE_PD;
        }

        NEXUS_ASSERT(cur_pd - full_message_id <=
                         NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_BEFORE_PD,
                     "Impossible message ID; outside of window (too small)");
    }
    return pd_increased;
}

/**
 * \param full_message_id integer value of message id to check
 * IFF message_id is valid, returns value of message_id bit in the bitmask
 */
bool nexus_keycode_pro_get_full_message_id_flag(const uint16_t full_message_id)
{
    (void) nexus_nv_read(NX_NV_BLOCK_KEYCODE_PRO,
                         (uint8_t*) &_nexus_keycode_stored);

    uint8_t mask_id_index = 0; // should never apply this default value.
    struct nexus_bitset received_ids;
    bool return_val = false;

    if (full_message_id > nexus_keycode_pro_get_current_pd_index())
    {
        return false;
    }

    // based on the current Pd value; determine if this is set
    else
    {
        const bool mask_id_valid = nexus_keycode_pro_mask_idx_from_message_id(
            full_message_id, &mask_id_index);
        if (mask_id_valid)
        {
            nexus_bitset_init(&received_ids,
                              _nexus_keycode_stored.flags_0_23.received_flags,
                              sizeof(_nexus_keycode_stored));

            return_val = nexus_bitset_contains(&received_ids, mask_id_index);
        }
    }
    return return_val;
}

/* INTERNAL */
static void _update_keycode_pro_nv_blocks(void)
{
    (void) nexus_nv_update(NX_NV_BLOCK_KEYCODE_PRO,
                           (uint8_t*) &_nexus_keycode_stored);
}

/**
 * \param full_message_id integer value of message id to set
 * \return void
 */
void nexus_keycode_pro_set_full_message_id_flag(const uint16_t full_message_id)
{
    // return if the bit is already set (don't waste an NVWrite)
    // also implicitly reads latest message_ids from NVRAM
    if (!nexus_keycode_pro_get_full_message_id_flag(full_message_id))
    {
        // this should always be overwritten.
        uint8_t mask_id_index = 0;

        struct nexus_bitset received_ids;
        nexus_bitset_init(&received_ids,
                          _nexus_keycode_stored.flags_0_23.received_flags,
                          NEXUS_KEYCODE_PRO_MAX_MESSAGE_ID_BYTE);

        (void) nexus_keycode_pro_update_window_and_message_mask_id(
            full_message_id, &mask_id_index);

        // mark the message as now applied
        nexus_bitset_add(&received_ids, mask_id_index);
        // nexus_bitset_remove(&received_ids, message_id);

        _update_keycode_pro_nv_blocks();
    }
}

/**
 * \param full_message_id id below which all received flags will be set.
 * \return void
 */
void nexus_keycode_pro_mask_below_message_id(const uint16_t full_message_id)
{
    // do not attempt to mask below full message ID 0.
    if (full_message_id == 0)
    {
        return;
    }

    const uint16_t max_full_id_to_mask = (uint16_t)(full_message_id - 1);
    uint8_t mask_id_index;

    // need to ensure mask_id_index is never set to a value larger than 255
    NEXUS_STATIC_ASSERT(
        NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_BEFORE_PD + 1 < UINT8_MAX,
        "NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_BEFORE_PD too large!");

    // don't mask anything -- full_message_id is invalid/below window.
    if (full_message_id < nexus_keycode_pro_get_current_pd_index() -
                              NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_BEFORE_PD)
    {
        return;
    }

    // update the window to ensure that Pd is >= the max ID to mask
    (void) nexus_keycode_pro_update_window_and_message_mask_id(
        max_full_id_to_mask, &mask_id_index);

    // otherwise, mask all masks up to and including the 'max_full_id_to_mask'.
    struct nexus_bitset received_ids;
    nexus_bitset_init(&received_ids,
                      _nexus_keycode_stored.flags_0_23.received_flags,
                      NEXUS_KEYCODE_PRO_MAX_MESSAGE_ID_BYTE);

    for (uint8_t i = 0; i <= mask_id_index; i++)
    {
        nexus_bitset_add(&received_ids, i);
    }

    _update_keycode_pro_nv_blocks();
}

void nexus_keycode_pro_reset_pd_index(void)
{
    // reset PD back to initial value.
    _nexus_keycode_stored.code_counts.pd_index =
        NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_BEFORE_PD;
}

// Wrapper to wipe all message IDs (zero'd)
void nexus_keycode_pro_wipe_message_ids_in_window(void)
{
    struct nexus_bitset received_ids;
    nexus_bitset_init(&received_ids,
                      _nexus_keycode_stored.flags_0_23.received_flags,
                      NEXUS_KEYCODE_PRO_MAX_MESSAGE_ID_BYTE);
    nexus_bitset_clear(&received_ids);

    NEXUS_STATIC_ASSERT(NEXUS_KEYCODE_PRO_MAX_MESSAGE_ID_BYTE * 8 ==
                            NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_BEFORE_PD + 1,
                        "NEXUS_KEYCODE_PRO_MAX_MESSAGE_ID_BYTE does not align "
                        "with WINDOW_BEFORE_PD!");

    _update_keycode_pro_nv_blocks();
}

void nexus_keycode_pro_reset_test_code_count(void)
{
    _nexus_keycode_stored.code_counts.qc_test_codes_received = 0;
}

NEXUS_IMPL_STATIC bool
nexus_keycode_pro_can_unit_accept_qc_code(const uint32_t qc_credit_seconds)
{
    uint8_t short_code_count = nexus_keycode_pro_get_short_qc_code_count();
    uint8_t long_code_count = nexus_keycode_pro_get_long_qc_code_count();
    const bool is_short_code =
        qc_credit_seconds <= NEXUS_KEYCODE_PRO_QC_SHORT_TEST_MESSAGE_SECONDS;

    const enum nxp_core_payg_state payg_state_before =
        nxp_core_payg_state_get_current();

    if (payg_state_before == NXP_CORE_PAYG_STATE_UNLOCKED)
    {
        return false;
    }

    // Don't allow test codes shorter than an hour to 'stack'
    if (qc_credit_seconds != NEXUS_KEYCODE_PRO_QC_LONG_TEST_MESSAGE_SECONDS &&
        payg_state_before != NXP_CORE_PAYG_STATE_DISABLED)
    {
        return false;
    }

    if (is_short_code &&
        short_code_count < NEXUS_KEYCODE_PRO_FACTORY_QC_SHORT_LIFETIME_MAX)
    {
        return true;
    }
    if (!is_short_code &&
        (long_code_count < NEXUS_KEYCODE_PRO_FACTORY_QC_LONG_LIFETIME_MAX))
    {
        return true;
    }

    return false;
}
/* Increment the (persisted) count of factory test messages received.
 * This count is used to limit the application of potentially dangerous,
 * universally applicable messages over the lifetime of a product.
 */
NEXUS_IMPL_STATIC uint8_t nexus_keycode_pro_get_long_qc_code_count(void)
{
    // Long code occupies up 4-MSB (0b11110000)
    uint8_t long_qc_code_count =
        (_nexus_keycode_stored.code_counts.qc_test_codes_received & 0xF0) >> 4;
    return long_qc_code_count;
}

NEXUS_IMPL_STATIC uint8_t nexus_keycode_pro_get_short_qc_code_count(void)
{
    // Short code occupies 4-LSB (0b00001111)
    uint8_t short_qc_code_count =
        _nexus_keycode_stored.code_counts.qc_test_codes_received & 0x0F;
    return short_qc_code_count;
}

NEXUS_IMPL_STATIC void
nexus_keycode_pro_increment_long_qc_test_message_count(void)
{
    const uint8_t new_long_code_count =
        (uint8_t)(nexus_keycode_pro_get_long_qc_code_count() + 1);
    // 15 is cap enforced by storage size of variable
    if (new_long_code_count > 15 ||
        new_long_code_count > NEXUS_KEYCODE_PRO_FACTORY_QC_LONG_LIFETIME_MAX)
    {
        return;
    }
    // "Erase" the existing long QC code count by masking it out
    _nexus_keycode_stored.code_counts.qc_test_codes_received &= 0x0F;
    _nexus_keycode_stored.code_counts.qc_test_codes_received |=
        (uint8_t)(new_long_code_count << 4);
    _update_keycode_pro_nv_blocks();
}

NEXUS_IMPL_STATIC void
nexus_keycode_pro_increment_short_qc_test_message_count(void)
{
    const uint8_t new_short_code_count =
        (uint8_t)(nexus_keycode_pro_get_short_qc_code_count() + 1);
    // 15 is cap enforced by storage size of variable
    if (new_short_code_count > 15 ||
        new_short_code_count > NEXUS_KEYCODE_PRO_FACTORY_QC_SHORT_LIFETIME_MAX)
    {
        return;
    }
    // "Erase" the existing short  QC code count by masking it out
    _nexus_keycode_stored.code_counts.qc_test_codes_received &= 0xF0;
    _nexus_keycode_stored.code_counts.qc_test_codes_received |=
        (new_short_code_count);
    _update_keycode_pro_nv_blocks();
}
