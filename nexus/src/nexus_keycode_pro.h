/** \file
 * Nexus Keycode Protocol Module (Header)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#ifndef NEXUS__KEYCODE__SRC__KEYCODE_PRO_H__
#define NEXUS__KEYCODE__SRC__KEYCODE_PRO_H__

#include "src/internal_keycode_config.h"

#if NEXUS_KEYCODE_ENABLED

    #include "src/nexus_keycode_mas.h"
    #include "src/nexus_nv.h"
    #include "src/nexus_util.h"

    #include <stdint.h>

    #ifdef __cplusplus
extern "C" {
    #endif

    //
    // PROTOCOL SPECIFIC CONSTANTS
    //

    // Defined here to be exposed for static asserts. Common to both protocols.
    #define NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_BEFORE_PD 23
    #define NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_AFTER_PD 40

//
// Common to both protocol variants
//
extern const uint32_t NEXUS_KEYCODE_PRO_QC_LONG_TEST_MESSAGE_SECONDS;
extern const uint8_t NEXUS_KEYCODE_PRO_UNIVERSAL_SHORT_TEST_SECONDS;
extern const uint32_t NEXUS_KEYCODE_PRO_SECONDS_IN_DAY;

//
// **Small* Protocol constants
//
extern const uint8_t NEXUS_KEYCODE_MESSAGE_LENGTH_MAX_DIGITS_SMALL;
extern const uint8_t NEXUS_KEYCODE_PRO_SMALL_MAX_TEST_FUNCTION_ID;

// increment_ids with special meaning for small set_credit messages
extern const uint8_t NEXUS_KEYCODE_PRO_SMALL_SET_CLEAR_CUSTOM_FLAG_RESTRICTED;
extern const uint8_t NEXUS_KEYCODE_PRO_SMALL_SET_LOCK_INCREMENT_ID;
extern const uint8_t NEXUS_KEYCODE_PRO_SMALL_SET_UNLOCK_INCREMENT_ID;

// alphabet length
extern const uint8_t NEXUS_KEYCODE_PRO_SMALL_ALPHABET_LENGTH;

// preserved for backwards compatibility
extern const uint16_t NEXUS_KEYCODE_PRO_SMALL_UNLOCK_INCREMENT;

    //
    // **Full** Protocol constants
    //
    #define NEXUS_KEYCODE_MESSAGE_LENGTH_MAX_DIGITS_FULL 30
extern const uint8_t NEXUS_KEYCODE_PRO_FULL_ALPHABET_LENGTH;
extern const uint32_t NEXUS_KEYCODE_PRO_FULL_UNLOCK_INCREMENT;
extern const uint32_t NEXUS_KEYCODE_PRO_QC_SHORT_TEST_MESSAGE_SECONDS;

    #define NEXUS_KEYCODE_PRO_FULL_ACTIVATION_BODY_CHARACTER_COUNT 8
// 6 check/MAC chars (in both Factory and Activation messages)
extern const uint8_t NEXUS_KEYCODE_PRO_FULL_CHECK_CHARACTER_COUNT;
// 8 to 10 chars Device ID for NOMAC_DEVICE_ID_CONFIRMATION message
extern const uint8_t NEXUS_KEYCODE_PRO_FULL_DEVICE_ID_MIN_CHARACTER_COUNT;
extern const uint8_t NEXUS_KEYCODE_PRO_FULL_DEVICE_ID_MAX_CHARACTER_COUNT;

//
// KEYCODE PROTOCOLS CORE
//

// A function that takes a keycode frame, and returns a
// `nexus_keycode_pro_response`
typedef enum nexus_keycode_pro_response (*nexus_keycode_pro_parse_and_apply)(
    const struct nexus_keycode_frame* frame);

// A function that takes an alphabet, and returns nothing
typedef void (*nexus_keycode_pro_protocol_init)(const char* alphabet);

void nexus_keycode_pro_init(nexus_keycode_pro_parse_and_apply parse_and_apply,
                            nexus_keycode_pro_protocol_init protocol_init,
                            const char* alphabet);
void nexus_keycode_pro_deinit(void);
void nexus_keycode_pro_enqueue(const struct nexus_keycode_frame* frame);
uint32_t nexus_keycode_pro_process(void);

//
// SMALL-ALPHABET PROTOCOL
//

enum nexus_keycode_pro_small_type_codes
{
    NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_ADD_CREDIT_TYPE = 0,
    // small passthrough not processed directly by `nexus_keycode_pro`, passed
    // to another handler (after deobfuscating/unscrambling transmitted
    // content)
    NEXUS_KEYCODE_PRO_SMALL_TYPE_PASSTHROUGH = 1,
    NEXUS_KEYCODE_PRO_SMALL_ACTIVATION_SET_CREDIT_TYPE = 2,
    NEXUS_KEYCODE_PRO_SMALL_MAINTENANCE_OR_TEST_TYPE = 3,
};

NEXUS_PACKED_STRUCT nexus_keycode_pro_small_message_body_activation
{
    uint8_t increment_id;
};

NEXUS_PACKED_STRUCT nexus_keycode_pro_small_message_body_maintenance_test
{
    uint8_t function_id; // MSB = 'is_maintenance' flag, 7-LSB = function ID
};

NEXUS_PACKED_UNION nexus_keycode_pro_small_message_body
{
    struct nexus_keycode_pro_small_message_body_activation activation;
    struct nexus_keycode_pro_small_message_body_maintenance_test
        maintenance_test;
};

NEXUS_PACKED_STRUCT nexus_keycode_pro_small_message
{
    uint32_t full_message_id; // Expanded message ID
    uint8_t type_code; // 2 bits (max value 3)
    union nexus_keycode_pro_small_message_body body;
    uint16_t check; // 12 MAC/check, 4 padding
};

enum nexus_keycode_pro_small_maintenance_functions
{
    NEXUS_KEYCODE_PRO_SMALL_WIPE_STATE_TARGET_CREDIT = 0x0000,
    NEXUS_KEYCODE_PRO_SMALL_WIPE_STATE_TARGET_CREDIT_AND_MASK = 0x0001,
    NEXUS_KEYCODE_PRO_SMALL_WIPE_STATE_TARGET_MASK = 0x0002,
};

enum nexus_keycode_pro_small_test_functions
{
    NEXUS_KEYCODE_PRO_SMALL_ENABLE_SHORT_TEST = 0x0000,
    NEXUS_KEYCODE_PRO_SMALL_ENABLE_QC_TEST = 0x0001,
};

void nexus_keycode_pro_small_init(const char* alphabet);

// return the current state of the underlying message ID window in a modifiable
// format (used to allow keycode_pro_extended functions to iterate through
// current windowed IDs)
void nexus_keycode_pro_get_current_message_id_window(
    struct nexus_window* window);

    #ifdef NEXUS_INTERNAL_IMPL_NON_STATIC
bool nexus_keycode_pro_small_parse(
    const struct nexus_keycode_frame* frame,
    struct nexus_keycode_pro_small_message* parsed);
uint32_t
nexus_keycode_pro_infer_full_message_id(const uint8_t compressed_message_id,
                                        const uint32_t current_pd_index,
                                        const uint8_t valid_id_count_below,
                                        const uint8_t valid_id_count_above);

void nexus_keycode_pro_increase_pd_and_shift_window_right(
    const uint32_t pd_increment);

enum nexus_keycode_pro_response nexus_keycode_pro_small_apply(
    const struct nexus_keycode_pro_small_message* message);

uint16_t nexus_keycode_pro_small_compute_check(
    const struct nexus_keycode_pro_small_message* message,
    const struct nx_common_check_key* key);
    #endif

// exposed for keycode_pro_extended
uint16_t
nexus_keycode_pro_small_get_set_credit_increment_days(uint8_t increment_id);

enum nexus_keycode_pro_response nexus_keycode_pro_small_parse_and_apply(
    const struct nexus_keycode_frame* frame);

//
// FULL-KEYPAD PROTOCOL
//

// parsed message layouts

NEXUS_PACKED_STRUCT nexus_keycode_pro_full_activation_add_set_credit
{
    uint32_t hours; // value from 5 digits
};

enum nexus_keycode_pro_full_wipe_state_target_codes
{
    NEXUS_KEYCODE_PRO_FULL_WIPE_STATE_TARGET_CREDIT = 0x00, // PAYG credit only
    NEXUS_KEYCODE_PRO_FULL_WIPE_STATE_TARGET_CREDIT_AND_MASK =
        0x01, // credit + message IDs
    NEXUS_KEYCODE_PRO_FULL_WIPE_STATE_TARGET_MASK_ONLY =
        0x02, // message IDS only
    NEXUS_KEYCODE_PRO_FULL_WIPE_CUSTOM_FLAG_RESTRICTED =
        0x03, // Wipe custom 'restricted' flag
};

NEXUS_PACKED_STRUCT nexus_keycode_pro_full_activation_wipe_state
{
    // value from enum nexus_keycode_pro_full_action_wipe_state_target_codes
    uint32_t target;
};

NEXUS_PACKED_STRUCT nexus_keycode_pro_full_factory
{
    uint32_t reserved;
};

NEXUS_PACKED_STRUCT nexus_keycode_pro_full_factory_qc_code
{
    // value from last 2 digits of 5 digit body
    uint32_t minutes;
};

NEXUS_PACKED_STRUCT nexus_keycode_pro_full_factory_nomac_device_id
{
    uint32_t device_id;
};

// Note: Passthrough command messages don't have a parsed body, only a type
// ID (0x08). This is because once the type ID is identified as Passthrough
// Command, no further processing of the message/keycode contents is
// performed in the library, and the raw data is passed to the product code.

NEXUS_PACKED_UNION nexus_keycode_pro_full_message_body
{
    struct nexus_keycode_pro_full_activation_add_set_credit add_set_credit;
    struct nexus_keycode_pro_full_activation_wipe_state wipe_state;
    struct nexus_keycode_pro_full_factory factory;
    struct nexus_keycode_pro_full_factory_qc_code qc_variant;
    struct nexus_keycode_pro_full_factory_nomac_device_id nexus_device_id;
};

enum nexus_keycode_pro_full_message_type_codes
{
    // 14-digit messages
    NEXUS_KEYCODE_PRO_FULL_ACTIVATION_ADD_CREDIT = 0x00,
    NEXUS_KEYCODE_PRO_FULL_ACTIVATION_SET_CREDIT = 0x01,
    NEXUS_KEYCODE_PRO_FULL_ACTIVATION_WIPE_STATE = 0x02,
    NEXUS_KEYCODE_PRO_FULL_ACTIVATION_DEMO_CODE = 0x03,
    // 7- to 13-digit message
    NEXUS_KEYCODE_PRO_FULL_FACTORY_ALLOW_TEST = 0x04,
    NEXUS_KEYCODE_PRO_FULL_FACTORY_QC_TEST = 0x05,
    NEXUS_KEYCODE_PRO_FULL_FACTORY_DEVICE_ID_DISPLAY = 0X06,
    NEXUS_KEYCODE_PRO_FULL_FACTORY_NOMAC_DEVICE_ID_CONFIRMATION = 0x07,
    NEXUS_KEYCODE_PRO_FULL_PASSTHROUGH_COMMAND = 0x08,
    // Type ID 9 is reserved (must never have more than 0-9 defined!)
};

NEXUS_PACKED_STRUCT nexus_keycode_pro_full_message
{
    uint32_t full_message_id; // Expanded message ID
    uint8_t type_code; // enum nexus_keycode_pro_full_message_type_codes
    union nexus_keycode_pro_full_message_body body;
    uint32_t check; // actual check value; not chars/digits
};

// protocol methods
void nexus_keycode_pro_full_init(const char* alphabet);
enum nexus_keycode_pro_response
nexus_keycode_pro_full_parse_and_apply(const struct nexus_keycode_frame* frame);
bool nexus_keycode_pro_full_parse(
    struct nexus_keycode_frame* frame,
    struct nexus_keycode_pro_full_message* parsed);

/* Get the value of the current "Pd Index" of the window.
 *
 * Defaults to 23 initially, and increases when any message is received
 * which has an ID larger than Pd.
 *
 * \return integer value of 'Pd Index' of current keycode receipt window
 */
uint32_t nexus_keycode_pro_get_current_pd_index(void);

    #ifdef NEXUS_INTERNAL_IMPL_NON_STATIC
uint32_t nexus_keycode_pro_full_check_field_from_frame(
    const struct nexus_keycode_frame* frame);
bool nexus_keycode_pro_full_parse_activation(
    struct nexus_keycode_frame* frame,
    struct nexus_keycode_pro_full_message* parsed);
bool nexus_keycode_pro_full_parse_factory_and_passthrough(
    const struct nexus_keycode_frame* frame,
    struct nexus_keycode_pro_full_message* parsed);
void nexus_keycode_pro_small_replace_old_short_test_code(
    struct nexus_keycode_pro_small_message* message);
enum nexus_keycode_pro_response nexus_keycode_pro_full_apply(
    const struct nexus_keycode_pro_full_message* message);
enum nexus_keycode_pro_response nexus_keycode_pro_full_apply_activation(
    const struct nexus_keycode_pro_full_message* message);
enum nexus_keycode_pro_response nexus_keycode_pro_full_apply_factory(
    const struct nexus_keycode_pro_full_message* message);
void nexus_keycode_pro_full_deinterleave(struct nexus_keycode_frame* frame,
                                         const uint32_t check_value);
uint32_t nexus_keycode_pro_full_compute_check(
    const struct nexus_keycode_pro_full_message* message,
    const struct nx_common_check_key* key);

/* Passthrough raw bits received in a small protocol passthrough message.
 *
 * Currently internal-only API to pipe certain messages from small protocol
 * to Nexus Channel origin command manager for handling.
 *
 * Will fully process (or copy and defer processing) contents of
 * `passthrough_bitstream` before returning.
 *
 * \param passthrough_bitstream bits to passthrough from smallpad protocol
 * \return error if problem occurred processing passthrough bitstream
 */
enum nxp_keycode_passthrough_error
nexus_keycode_pro_small_internal_bitstream_passthrough(
    struct nexus_bitstream* passthrough_bitstream);

/* Take input bitstream and PRNG bitstream, create 'obfuscated' output bitstream
 *
 * XORs each bit of `input_bitstream` with `prng_bitstream` up to
 * `obfuscation_bit_count` and writes the result to `output_bitstream`.
 *
 * If there are any remaining bits in `input_bitstream` to consume, they are
 * copied directly (not obfuscated) to `output_bitstream`.
 *
 * Calling this function twice in a row, where `input_bitstream` of the
 * *second* execution is the `output_bitstream` of the *first* execution,
 * will result in a final `output_bitstream` equal to the first
 * `input_bitstream`.
 *
 * That is, this function performs a reversible obfuscation.
 *
 * `output_bitstream` must be at least as large as `input_bitstream`.
 *
 * \param input_bitstream input bitstream to obscure
 * \param output_bitstream output bitstream to write to
 * \param prng_bitstream bits used to 'obscure' input bitstream (via bitwise XOR)
 * \param obfuscation_bit_count number of bits of `input_bitstream` to XOR with `prng_bitstream`
 */
void nexus_keycode_pro_small_reversibly_obfuscate(
    struct nexus_bitstream* input_bitstream,
    struct nexus_bitstream* output_bitstream,
    struct nexus_bitstream* prng_bitstream,
    uint8_t obfuscation_bit_count);

/* Take input bitstream of 28 bits extract 26 bit passthrough message
 *
 * Assumes that type ID of `input_message_bitstream` has already been checked
 * and is a passthrough (small protocol) message. Assumes that the input
 * has already been deobscured.
 *
 * Only 26 of the 28 bits from the input bitstream will be copied to the output
 * bitstream (eliding the smallpad type ID).
 *
 * \param passthrough_bitstream bitstream to populate (at least 26 bits in size)
 * \param deobscured_bitstream input to extract passthrough message from
 *
 */
void _nexus_keycode_pro_small_internal_extract_passthrough_message(
    struct nexus_bitstream* passthrough_bitstream,
    struct nexus_bitstream* deobscured_bitstream);

/* Determine if a given message ID value is within the current receipt
 * window.
 *
 * Returns true iff the following is true:
 *  pd_index - NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_BEFORE_PD <=
 *  full_message_id <= pd_index + NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_AFTER_PD
 *
 * \param full_message_id message ID to check for validity within window
 * \return true iff the full_message_id is within the current window
 */
bool nexus_keycode_pro_is_message_id_within_window(
    const uint16_t full_message_id);

/* Retrieve the window mask ID index for a given full message ID.
 *
 * If full_message_id is within the current valid receipt window, this
 * function will return 'true', and mask_id_index will be modified to the
 * index within the window which corresponds to
 *
 * This function does not modify the window or mask value in any way, it
 * only provides the mask index corresponding to full_message_id, if it
 * exists.
 *
 * If this function returns 'false', the value of mask_id_index should be
 * considered invalid and ignored.
 *
 * Note that mask_id_index as returned by this function may be any value
 * within the window (not only below Pd).
 *
 * \param full_message_id message ID for which to retrieve mask_id_index
 * \param mask_id_index pointer to location to store mask_id_index, if valid
 * \return false if mask_id_index does not exist in current window, else true
 */
bool nexus_keycode_pro_mask_idx_from_message_id(const uint16_t full_message_id,
                                                uint8_t* mask_id_index);

/* Internally used to move the window to the right, and return the new mask
 * ID index when this occurs.
 *
 * \param full_message_id full message ID to update PD to
 * \param mask_id_index pointer to location to store mask_id_index for
 *full_message_id
 * \return true if window shifted to the right (pd increased) false
 *otherwise
 */
bool nexus_keycode_pro_update_window_and_message_mask_id(
    const uint16_t full_message_id, uint8_t* mask_id_index);
/* Checks if the unit can accept an QC code.
 *
 * Checks if the unit is unlocked, if the QC code is under 1h and the unit
 * is PAYG Disabled, and if the code count limits have been reached.
 */
bool nexus_keycode_pro_can_unit_accept_qc_code(
    const uint32_t qc_credit_seconds);
/* Returns the unit's current short (under 10min) QC code count. Limit is
 * set by a the constant 'NEXUS_KEYCODE_PRO_FACTORY_QC_SHORT_LIFETIME_MAX'.
 *
 * Used within the preceding function to determine if the limit has been
 * reached.
 */
uint8_t nexus_keycode_pro_get_short_qc_code_count(void);
/* Returns the unit's current long (between 10min and 1h) QC code count.
 * Limit is set by the constant
 * 'NEXUS_KEYCODE_PRO_QC_LONG_TEST_MESSAGE_LIFETIME_MAX'.
 *
 * Used within the 'can_unit_accept_qc_code' function to detemine if the
 * limit has been reached.
 */
uint8_t nexus_keycode_pro_get_long_qc_code_count(void);
/* Increments the unit's QC short code count by one.
 */
void nexus_keycode_pro_increment_short_qc_test_message_count(void);
/* Increments the unit's QC long code count by one.
 */
void nexus_keycode_pro_increment_long_qc_test_message_count(void);
    #endif

/* Get the value of the receipt mask associated with a given message ID.
 *
 * This function is used to determine if a given message has already been
 * applied or not.  It does not require knowledge of the current window,
 * and assumes if it is given a full message ID below the window, that the
 * message was already received by the unit.
 *
 * If the mask in the current window associated with full_message_id is set,
 * this function returns true.
 *
 * Otherwise, this function returns 'false'.
 *
 * \param full_message_id value of message ID to check against.
 * \returns true IFF associated mask in window is set, or full message ID is
 *below window
 */
bool nexus_keycode_pro_get_full_message_id_flag(const uint16_t full_message_id);

/* Set the mask flag for a full message ID, and update window if required.
 *
 * Given a full (uncompressed) keycode message ID, set the 'received' mask
 * value for this message ID.
 *
 * If the message ID is above the current keycode message ID receipt window,
 * this function will update the window (effectively updating Pd to the
 * value of full_message_id), and then set the received flag value.
 *
 * The full_message_id passed to this function must be greater than or
 * equal to the lowest message ID contained within the current receipt
 * window, or this function will have no effect.
 *
 * In other words, this function may, if required, shift the window
 * 'upwards', that is, 'to the right' (Pd increases).  This function will
 * never shift the window 'to the left' (Pd decreases).
 *
 * \warning if the full_message_id is below the current receipt window, this
 * will silently fail, and no modification will take place.
 *
 * \param full_message_id value of message ID for which to (un)set flag value.
 * \param set_value will set mask/flag if true, will unset mask/flag if false.
 *
 * \returns void
 */
void nexus_keycode_pro_set_full_message_id_flag(const uint16_t full_message_id);

/* Reset the mask flag for a full message ID, if it is within current
 * window.
 *
 * Given a full (uncompressed) keycode message ID, reset the 'received' mask
 * value for this message ID.
 *
 * This function will never modify the current Pd index, or shift the
 * receipt window in any way.  The full_message_id must be within the
 * current valid receipt window, or this function will have no effect.
 *
 * \warning if the full_message_id is outside of the current receipt window,
 * this function will silently fail, and no modification will take place.
 *
 * \param full_message_id value of message ID for which to unset flag value.
 * \returns void
 */
void nexus_keycode_pro_reset_full_message_id(const uint16_t full_message_id);

/* Set the received mask flag for all message IDs below full_message_id.
 *
 * Given a full (uncompressed) keycode message ID, set all message flags
 * below this one to 'received'.  The flag associated with 'full_message_id'
 * itself is not set.
 *
 * For example, calling this function with 'full_message_id' == 0 will never
 * have any effect on the received mask values.
 *
 * If full_message_id is greater than the current Pd index, the Pd index
 * will be updated, and  the window will shift 'to the right' to allow this
 * masking operation to occur.
 *
 * If full message ID is below the current window, no action is taken, as
 * a keycode ID outside the current window is already considered
 * set/invalid.
 *
 * \warning this function *will* shift the window 'to the right' (increasing
 * pd') if full_message_id is greater than the current Pd index.
 *
 * \param full_message_id value of message
 * \returns void
 */
void nexus_keycode_pro_mask_below_message_id(const uint16_t full_message_id);

/* Reset the 'center' of the message receipt window ("Pd")
 *
 * When this function is called; "Pd" is reset to its default value,
 * effectively resetting the message receipt window position to "factory
 * default".
 */
void nexus_keycode_pro_reset_pd_index(void);

/* Used to 'reset' the flags within the keycode ID receipt window mask.
 *
 * All message receipt flags within the current receipt window are reset to
 * '0'.  Calling this function in addition to
 * `nexus_keycode_pro_reset_pd_index' is the same as resetting the entire
 * keycode receipt state of the unit to 'factory default'.
 *
 * This function has no impact on the PAYG credit or PAYG state of the unit.
 */
void nexus_keycode_pro_wipe_message_ids_in_window(void);

/* Used to 'forget' that any test codes were applied to this device.
 *
 * The total test code count is reset to zero.
 */
void nexus_keycode_pro_reset_test_code_count(void);

/* Used to reset an internal 'custom_flag' stored by the keycode_pro module.
 *
 * \param flag `nx_keycode_custom_flag` to reset
 */
void nexus_keycode_pro_reset_custom_flag(enum nx_keycode_custom_flag flag);

// always uint32_t full_message body
NEXUS_STATIC_ASSERT(
    sizeof(((struct nexus_keycode_pro_full_message*) 0)->body) == 4,
    "expected nexus_keycode_pro_full_message *body* size incorrect");
NEXUS_STATIC_ASSERT(sizeof(struct nexus_keycode_pro_full_message) == 13,
                    "expected nexus_keycode_pro_full_message size incorrect");

    #ifdef __cplusplus
}
    #endif

#endif /* if NEXUS_KEYCODE_ENABLED */
#endif /* ifndef NEXUS__KEYCODE__SRC__KEYCODE_PRO_H__ */
