/** \file nexus_channel_om.c
 * Nexus Channel Origin Messaging Module (Implementation)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "src/nexus_channel_om.h"
#include "include/nxp_channel.h"
#include "src/nexus_channel_core.h"
#include "src/nexus_nv.h"
#include "src/nexus_util.h"

#if NEXUS_CHANNEL_ENABLED
#if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE

// Windowing scheme:
// All Origin commands are created using a 'command ID flag'
// [0 ... 32 ... 40]
// Center is at '32', recognizes messages with command IDs between 0 and 40
// If receiving a valid command which has not previously been received, set
// a flag preventing future attempts to apply the same command, and update
// NV.

// recognize up to 31 "OM command counts" behind the command count center
#define NEXUS_CHANNEL_OM_RECEIVE_WINDOW_BEFORE_CENTER_INDEX 31
// and 8 'beyond' the current command count
#define NEXUS_CHANNEL_OM_RECEIVE_WINDOW_AFTER_CENTER_INDEX 8

// Number of flags stored [32] / CHAR_BIT [8]
#define NEXUS_CHANNEL_OM_MAX_RECEIVE_FLAG_BYTE 4

static NEXUS_PACKED_STRUCT
{
    // center 'index' of window of received commands
    uint32_t command_index;
    NEXUS_PACKED_STRUCT
    {
        // mark received IDs from '0' position in the window to '31' position
        uint8_t received_ids[NEXUS_CHANNEL_OM_MAX_RECEIVE_FLAG_BYTE];
    }
    flags_0_31;
}
_nexus_om_stored;

// Compile time checks
NEXUS_STATIC_ASSERT(
    sizeof(_nexus_om_stored) ==
        (NX_CORE_NV_BLOCK_3_LENGTH - NEXUS_NV_BLOCK_WRAPPER_SIZE_BYTES),
    "nexus_channel_om: _nexus_om_stored invalid size for NV block.");

NEXUS_STATIC_ASSERT(NEXUS_CHANNEL_OM_RECEIVE_WINDOW_BEFORE_CENTER_INDEX + 1 ==
                        NEXUS_CHANNEL_OM_MAX_RECEIVE_FLAG_BYTE * 8,
                    "Receive flag window improperly sized");

NEXUS_STATIC_ASSERT(
    (NEXUS_CHANNEL_OM_RECEIVE_WINDOW_AFTER_CENTER_INDEX +
     NEXUS_CHANNEL_OM_RECEIVE_WINDOW_BEFORE_CENTER_INDEX + 1) %
            8 ==
        0,
    "Channel OM window not divisible by 8, is window size incorrect?");

NEXUS_STATIC_ASSERT(sizeof(_nexus_om_stored) % 2 == 0,
                    "Packed struct does not have a size divisible by 2.");

void nexus_channel_om_init(void)
{
    _nexus_om_stored.command_index =
        NEXUS_CHANNEL_OM_RECEIVE_WINDOW_BEFORE_CENTER_INDEX;
    memset(&_nexus_om_stored.flags_0_31,
           0x00,
           sizeof(_nexus_om_stored.flags_0_31));

    (void) nexus_nv_read(NX_NV_BLOCK_CHANNEL_OM, (uint8_t*) &_nexus_om_stored);
}

NEXUS_IMPL_STATIC enum nexus_channel_om_command_type
_nexus_channel_om_ascii_validate_command_type(const uint8_t type_int)
{
    if (type_int > NEXUS_CHANNEL_OM_COMMAND_TYPE_ACCESSORY_ACTION_UNLINK &&
        type_int != NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3)
    {
        return NEXUS_CHANNEL_OM_COMMAND_TYPE_INVALID;
    }
    return (enum nexus_channel_om_command_type) type_int;
}

NEXUS_IMPL_STATIC bool _nexus_channel_om_ascii_extract_body_controller_action(
    struct nexus_digits* command_digits,
    struct nexus_channel_om_controller_action_body* body)
{
    // will be set to UINT8_MAX if pulling digit fails
    body->action_type = nexus_digits_pull_uint8(command_digits, 2);

    return (body->action_type != UINT8_MAX) &&
           nexus_digits_remaining(command_digits) > 0;
}

NEXUS_IMPL_STATIC bool _nexus_channel_om_ascii_extract_body_accessory_action(
    struct nexus_digits* command_digits,
    struct nexus_channel_om_accessory_action_body* body)
{
    // contains '1' truncated digit (least significant digit)
    body->trunc_acc_id.digits_count = 1;

    bool underrun = false;
    body->trunc_acc_id.digits_int = nexus_digits_try_pull_uint32(
        command_digits, body->trunc_acc_id.digits_count, &underrun);

    return !underrun && nexus_digits_remaining(command_digits) > 0;
}

NEXUS_IMPL_STATIC bool _nexus_channel_om_ascii_extract_body_create_link(
    struct nexus_digits* command_digits,
    struct nexus_channel_om_create_link_body* body)
{
    bool underrun = false;

    // contains '1' truncated digit (least significant digit)
    body->trunc_acc_id.digits_count = 1;

    body->trunc_acc_id.digits_int = nexus_digits_try_pull_uint32(
        command_digits, body->trunc_acc_id.digits_count, &underrun);

    body->accessory_challenge.six_int_digits =
        nexus_digits_try_pull_uint32(command_digits, 6, &underrun);

    return !underrun && nexus_digits_remaining(command_digits) > 0;
}

// Populates a message with all fields transmitted from the command digits.
NEXUS_IMPL_STATIC bool _nexus_channel_om_ascii_parse_message(
    struct nexus_digits* command_digits,
    struct nexus_channel_om_command_message* message)
{
    bool underrun = false;
    bool parsed = false;

    // Obtain command type, will be UINT8_MAX and fail validation if pull fails
    const uint8_t command_type_int = nexus_digits_pull_uint8(command_digits, 1);
    message->type =
        _nexus_channel_om_ascii_validate_command_type(command_type_int);

    // parse message body
    switch (message->type)
    {
        case NEXUS_CHANNEL_OM_COMMAND_TYPE_GENERIC_CONTROLLER_ACTION:
            parsed = _nexus_channel_om_ascii_extract_body_controller_action(
                command_digits, &message->body.controller_action);
            break;

        case NEXUS_CHANNEL_OM_COMMAND_TYPE_ACCESSORY_ACTION_UNLOCK:
        // intentional fallthrough
        case NEXUS_CHANNEL_OM_COMMAND_TYPE_ACCESSORY_ACTION_UNLINK:
            parsed = _nexus_channel_om_ascii_extract_body_accessory_action(
                command_digits, &message->body.accessory_action);
            break;

        case NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3:
            parsed = _nexus_channel_om_ascii_extract_body_create_link(
                command_digits, &message->body.create_link);
            break;

        case NEXUS_CHANNEL_OM_COMMAND_TYPE_INVALID:
        // intentional fallthrough

        default:
            NEXUS_ASSERT_FAIL_IN_DEBUG_ONLY(
                0, "Unsupported bearer_type - should not reach here.");
            // should never reach here
            break;
    }

    // we must have 6 digits remaining for the auth code.
    if (nexus_digits_remaining(command_digits) != 6)
    {
        parsed = false;
    }

    message->auth.six_int_digits =
        nexus_digits_try_pull_uint32(command_digits, 6, &underrun);

    // we parsed correctly and no digits are remaining to extract
    return !underrun && parsed && nexus_digits_remaining(command_digits) == 0;
}

// internal
static uint32_t _nexus_channel_om_ascii_auth_arbitrary_bytes(
    const uint8_t* bytes,
    uint8_t bytes_count,
    const struct nx_core_check_key* key)
{
    const struct nexus_check_value check_val =
        nexus_check_compute(key, bytes, bytes_count);

    // obtain lower 32 bits of check
    const uint32_t lower_check =
        nexus_check_value_as_uint64(&check_val) & 0xffffffff;

    // obtain the 'decimal representation' of the lowest 6 decimal digits of
    // the check. Note that leading zeros are *ignored* as the check is now
    // computed over the numeric value represented by the 6 decimal check
    // digits, not the individual digits themselves.
    return lower_check % 1000000;
}

NEXUS_IMPL_STATIC bool _nexus_channel_om_ascii_message_infer_inner_compute_auth(
    struct nexus_channel_om_command_message* message,
    const struct nx_core_check_key* origin_key)
{
    uint8_t compute_bytes[NEXUS_CHANNEL_OM_COMMAND_ASCII_MAX_BYTES_TO_AUTH] = {
        0};
    bool success = false;
    uint32_t computed_check;

    // first 4 bytes are the command ID for all message types
    //
    const uint32_t computed_command_id_le =
        nexus_endian_htole32(message->computed_command_id);
    memcpy(&compute_bytes, &computed_command_id_le, 4);

    NEXUS_ASSERT(sizeof(message->computed_command_id) == 4,
                 "Invalid command ID size");

    // 5th byte is just the command type code
    compute_bytes[4] = (uint8_t) message->type;

    uint8_t bytes_count = 5;

    if (message->type ==
        NEXUS_CHANNEL_OM_COMMAND_TYPE_GENERIC_CONTROLLER_ACTION)
    {
        const uint32_t action_type_le =
            nexus_endian_htole32(message->body.controller_action.action_type);
        memcpy(&compute_bytes[5], &action_type_le, 4);
        NEXUS_STATIC_ASSERT(
            sizeof(((struct nexus_channel_om_controller_action_body*) 0)
                       ->action_type) == 4,
            "Unexpected struct size for controller action body");
        bytes_count = (uint8_t)(bytes_count + 4);

        computed_check = _nexus_channel_om_ascii_auth_arbitrary_bytes(
            compute_bytes, bytes_count, origin_key);

        if (computed_check == message->auth.six_int_digits)
        {
            success = true;
        }
    }
    else if (message->type ==
                 NEXUS_CHANNEL_OM_COMMAND_TYPE_ACCESSORY_ACTION_UNLOCK ||
             message->type ==
                 NEXUS_CHANNEL_OM_COMMAND_TYPE_ACCESSORY_ACTION_UNLINK)
    {
        // XXX obtain list of nx_ids for devices linked to this controller
        // uint8_t accessory_count = 0;
        // struct nx_id linked_accessory_id =
        // nexus_core_get_linked_accessory_ids(&number_of-accessories);

        struct nx_id accessories_list[3] = {
            {0x0111, 0x92873891}, // nonsense
            {0x0102, 0x94837158}, // one used in tests
            {0x9041, 0x00000019}, // nonsense
        };
        uint8_t accessory_count = 3;
        const uint8_t digits_count =
            message->body.accessory_action.trunc_acc_id.digits_count;
        uint8_t trunc_digits_mod = 0;

        switch (digits_count)
        {
            case 0:
                trunc_digits_mod = 0;
                break;
            case 1:
                trunc_digits_mod = 10;
                break;
            default:
                NEXUS_ASSERT_FAIL_IN_DEBUG_ONLY(
                    0, "Unsupported truncated digits count (max 1)");
                break;
        }

        // need guaranteed consistent ordering across platforms
        uint16_t authority_id_le = 0;
        uint32_t device_id_le = 0;

        if (trunc_digits_mod < 10)
        {
            // invalid message, must have at least one truncated digit for
            // filtering.
            return false;
        }
        while (accessory_count > 0)
        {
            // XXX change once we are able to 'loop through' linked accessories
            // This is a hardcoded value used in tests, we should instead mock
            // this value in the function we will call here (for tests)
            const struct nx_id* accessory_id_ptr =
                &accessories_list[accessory_count - 1];
            accessory_count--;

            // Get the 'least significant' digits by using modulus
            if ((accessory_id_ptr->device_id % trunc_digits_mod) !=
                message->body.accessory_action.trunc_acc_id.digits_int)
            {
                // not a match, continue to check the next ID
                continue;
            }

            authority_id_le =
                nexus_endian_htole16(accessory_id_ptr->authority_id);
            memcpy(&compute_bytes[5],
                   &authority_id_le,
                   sizeof(accessory_id_ptr->authority_id));
            NEXUS_STATIC_ASSERT(sizeof(((struct nx_id*) 0)->authority_id) == 2,
                                "Invalid size for authority ID");
            bytes_count = (uint8_t)(bytes_count + 2);

            device_id_le = nexus_endian_htole32(accessory_id_ptr->device_id);

            memcpy(&compute_bytes[7],
                   &device_id_le,
                   sizeof(accessory_id_ptr->device_id));
            NEXUS_STATIC_ASSERT(sizeof(((struct nx_id*) 0)->device_id) == 4,
                                "Invalid size for device ID");

            bytes_count = (uint8_t)(bytes_count + 4);

            computed_check = _nexus_channel_om_ascii_auth_arbitrary_bytes(
                compute_bytes, bytes_count, origin_key);
            if (computed_check == message->auth.six_int_digits)
            {
                // populate the 'full' nexus ID of the linked accessory
                message->body.accessory_action.computed_accessory_id
                    .authority_id = accessory_id_ptr->authority_id;
                message->body.accessory_action.computed_accessory_id.device_id =
                    accessory_id_ptr->device_id;
                success = true;
            }
            else
            {
                // will re-increment on next iteration

                bytes_count = (uint8_t)(bytes_count - 6);
            }
        }
    }
    else if (message->type ==
             NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3)
    {
        NEXUS_ASSERT(message->body.create_link.trunc_acc_id.digits_count <= 2,
                     "Cannot support this many truncated digits");
        // Specification indicates truncated accessory ID for link mode 3 is
        // always 1 byte in size.
        compute_bytes[5] =
            (uint8_t)(message->body.create_link.trunc_acc_id.digits_int);
        bytes_count++;

        const uint32_t accessory_challenge_le = nexus_endian_htole32(
            message->body.create_link.accessory_challenge.six_int_digits);

        memcpy(&compute_bytes[6], &accessory_challenge_le, 4);
        NEXUS_STATIC_ASSERT(
            sizeof(((union nexus_channel_om_auth_field*) 0)->six_int_digits) ==
                4,
            "Invalid size for six_int_digits field");
        bytes_count = (uint8_t)(bytes_count + 4);

        NEXUS_ASSERT(bytes_count == 10,
                     "Invalid number of bytes for MAC computation");

        computed_check = _nexus_channel_om_ascii_auth_arbitrary_bytes(
            compute_bytes, bytes_count, origin_key);

        if (computed_check == message->auth.six_int_digits)
        {
            success = true;
        }
    }
    // sanity check for tests
    NEXUS_ASSERT(bytes_count <=
                     NEXUS_CHANNEL_OM_COMMAND_ASCII_MAX_BYTES_TO_AUTH,
                 "too many bytes to auth!");

    return success;
}

// returns true and populates inferred command ID and MAC if valid, returns
// false if not.
// If this returns true, message MAC can be assumed valid
NEXUS_IMPL_STATIC bool _nexus_channel_om_ascii_infer_fields_compute_auth(
    struct nexus_channel_om_command_message* message,
    const struct nexus_window* window,
    const struct nx_core_check_key* origin_key)
{
    NEXUS_ASSERT(window->center_index - window->flags_below <=
                     window->center_index + window->flags_above,
                 "No IDs to check/validate against");
    bool validated = false;

    // start counting from 'bottom' of window
    message->computed_command_id = window->center_index - window->flags_below;
    NEXUS_ASSERT(nexus_util_window_id_within_window(
                     window, message->computed_command_id),
                 "Bottom of window is outside of window - unexpected.");

    // loop through all possible command IDs in the window
    while (nexus_util_window_id_within_window(window,
                                              message->computed_command_id))
    {
        // only examine IDs that aren't already set
        if (!nexus_util_window_id_flag_already_set(
                window, message->computed_command_id) &&
            (_nexus_channel_om_ascii_message_infer_inner_compute_auth(
                message, origin_key)))
        {
            validated = true;
            // don't increment computed command ID any longer
            break;
        }
        message->computed_command_id++;
    }

    return validated;
}

// This function 'infers' any fields that are not the command ID
NEXUS_IMPL_STATIC bool _nexus_channel_om_ascii_apply_message(
    struct nexus_channel_om_command_message* message)
{
    const struct nx_core_check_key origin_key =
        nxp_channel_symmetric_origin_key();

    (void) nexus_nv_read(NX_NV_BLOCK_CHANNEL_OM, (uint8_t*) &_nexus_om_stored);
    struct nexus_window window;

    nexus_util_window_init(
        &window,
        _nexus_om_stored.flags_0_31.received_ids,
        NEXUS_CHANNEL_OM_MAX_RECEIVE_FLAG_BYTE,
        _nexus_om_stored.command_index, // center on current index
        NEXUS_CHANNEL_OM_RECEIVE_WINDOW_BEFORE_CENTER_INDEX,
        NEXUS_CHANNEL_OM_RECEIVE_WINDOW_AFTER_CENTER_INDEX);

    // Can we apply this message, or is it invalid or already used?
    if (!_nexus_channel_om_ascii_infer_fields_compute_auth(
            message, &window, &origin_key))
    {
        PRINT("nx_channel_om: Origin command already used or invalid\n");
        return false;
    }

    // Finally, attempt to send the message to core, return early
    // if we're unable to apply it for any reason
    if (!nexus_channel_core_apply_origin_command(message))
    {
        PRINT("nx_channel_om: Nexus Core could not apply origin command.\n");
        return false;
    }

    // If the channel core processed the message, mark it as applied/update NV

    (void) nexus_util_window_set_id_flag(&window, message->computed_command_id);
    NEXUS_ASSERT(message->computed_command_id <= window.center_index,
                 "Error setting command ID flag..");
    _nexus_om_stored.command_index = window.center_index;
    // will only change if the window moved in the previous 'set id flag' step
    (void) nexus_nv_update(NX_NV_BLOCK_CHANNEL_OM,
                           (uint8_t*) &_nexus_om_stored);

    PRINT("nx_channel_om: Origin command was successfully applied!\n");
    return true;
}

// internal handlers for origin messages passed in from implementing product
NEXUS_IMPL_STATIC bool
_nexus_channel_om_handle_ascii_origin_command(const char* const command_data,
                                              const uint32_t command_length)
{
    struct nexus_digits command_digits;
    char digit_chars[NEXUS_CHANNEL_OM_COMMAND_ASCII_DIGITS_MAX_LENGTH] = {0};

    if (command_length > NEXUS_CHANNEL_OM_COMMAND_ASCII_DIGITS_MAX_LENGTH)
    {
        PRINT("nexus_channel_om: Origin command exceeds max command length.\n");
        return false;
    }

    // ensure command data is only ascii digits
    for (uint8_t i = 0; i < command_length; i++)
    {
        const uint8_t val = (uint8_t) * (command_data + i);
        // 0x30 = '0', 0x39 = '9'
        if (val < 0x30 || val > 0x39)
        {
            PRINT("nexus_channel_om: Origin command is not ASCII\n");
            return false;
        }
    }

    // convert command data to known digits struct
    memcpy(&digit_chars, command_data, command_length);
    nexus_digits_init(&command_digits, digit_chars, (uint16_t) command_length);

    // parse message, containing:
    //
    // * 1-digit header/om_command_type
    // * N-digit body

    // * 6-digit MAC/auth
    struct nexus_channel_om_command_message message = {0};
    if (!_nexus_channel_om_ascii_parse_message(&command_digits, &message))
    {
        PRINT("nx_channel_om: Failed to parse origin command contents\n");

        // return early if parsing fails
        return false;
    }

    // We attempt to 'apply' the message. This consists of:
    // - Filling out 'inferred' (not transmitted) message parameters
    // - Computing the authentication for the message
    // - Determining if message is already applied (triggers NV read)
    // - Calling Core with an appropriate origin command (if valid)
    // - Marking the message as applied (NV update)
    if (!_nexus_channel_om_ascii_apply_message(&message))
    {
        PRINT("nx_channel_om: Failed to apply origin command\n");
        return false;
    }

    // If we completed the sequence and updated NV, return true
    return true;
}
#endif /* NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE */
#endif /* if NEXUS_CHANNEL_ENABLED */

nx_channel_error nx_channel_handle_origin_command(
    const enum nx_channel_origin_command_bearer_type bearer_type,
    const void* command_data,
    const uint32_t command_length)
{
// We include this stubbed function implementation in accessory mode to
// simplify the product-facing interface, but this function does nothing
// if controller mode is not supported.
#if (defined(NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE) &&                         \
     NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE)
    bool parsed = false;
    switch (bearer_type)
    {
        case NX_CHANNEL_ORIGIN_COMMAND_BEARER_TYPE_ASCII_DIGITS:
            PRINT("nx_channel_om: Handling origin command (bearer=ASCII "
                  "digits)\n");
            parsed = _nexus_channel_om_handle_ascii_origin_command(
                command_data, command_length);
            break;

        default:
            NEXUS_ASSERT_FAIL_IN_DEBUG_ONLY(
                0, "Unsupported bearer_type - should not reach here.");
            break;
    }

    if (parsed)
    {
        return NX_CHANNEL_ERROR_NONE;
    }
    else
    {
        return NX_CHANNEL_ERROR_ACTION_REJECTED;
    }
#else
    (void) bearer_type;
    (void) command_data;
    (void) command_length;
    return NX_CHANNEL_ERROR_UNSPECIFIED;
#endif /* if (defined(NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE) &&                \
          NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE) */
}

#ifdef NEXUS_INTERNAL_IMPL_NON_STATIC
// only used in unit tests
bool _nexus_channel_om_is_command_index_set(uint32_t command_index)
{
    struct nexus_window window;
    nexus_util_window_init(
        &window,
        _nexus_om_stored.flags_0_31.received_ids,
        NEXUS_CHANNEL_OM_MAX_RECEIVE_FLAG_BYTE,
        _nexus_om_stored.command_index, // center on current index
        NEXUS_CHANNEL_OM_RECEIVE_WINDOW_BEFORE_CENTER_INDEX,
        NEXUS_CHANNEL_OM_RECEIVE_WINDOW_AFTER_CENTER_INDEX);

    return nexus_util_window_id_flag_already_set(&window, command_index);
}

// only used in unit tests
bool _nexus_channel_om_is_command_index_in_window(uint32_t command_index)
{
    struct nexus_window window;
    nexus_util_window_init(
        &window,
        _nexus_om_stored.flags_0_31.received_ids,
        NEXUS_CHANNEL_OM_MAX_RECEIVE_FLAG_BYTE,
        _nexus_om_stored.command_index, // center on current index
        NEXUS_CHANNEL_OM_RECEIVE_WINDOW_BEFORE_CENTER_INDEX,
        NEXUS_CHANNEL_OM_RECEIVE_WINDOW_AFTER_CENTER_INDEX);

    return !((command_index < (window.center_index - window.flags_below)) ||
             (command_index > (window.center_index + window.flags_above)));
}
#endif /* ifdef NEXUS_INTERNAL_IMPL_NON_STATIC */
