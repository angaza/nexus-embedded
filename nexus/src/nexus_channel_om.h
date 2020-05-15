/** \file
 * Nexus Channel Origin Messaging Module (Header)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#ifndef NEXUS__SRC__CHANNEL__CHANNEL_OM_H_
#define NEXUS__SRC__CHANNEL__CHANNEL_OM_H_

#include "src/internal_channel_config.h"
#include "src/nexus_util.h"

#if NEXUS_CHANNEL_ENABLED
// Origin commands are only relevant for a Channel Controller
#if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE
#define NEXUS_CHANNEL_OM_COMMAND_ASCII_DIGITS_MAX_LENGTH 20
#define NEXUS_CHANNEL_OM_COMMAND_ASCII_MAX_BYTES_TO_AUTH 15
#define NEXUS_CHANNEL_OM_INVALID_COMMAND_COUNT 0xFFFFFFFF // sentinel

/*! \enum nexus_channel_om_command_type
 *
 * \brief Types of Nexus Channel Origin Commands
 *
 * Types 0-9 are possible to transmit via ASCII_DIGITS. Additional types may
 * exist in the future which are not easily transmitted via token.
 */
enum nexus_channel_om_command_type
{
    NEXUS_CHANNEL_OM_COMMAND_TYPE_GENERIC_CONTROLLER_ACTION = 0,
    NEXUS_CHANNEL_OM_COMMAND_TYPE_ACCESSORY_ACTION_UNLOCK = 1,
    NEXUS_CHANNEL_OM_COMMAND_TYPE_ACCESSORY_ACTION_UNLINK = 2,
    // 3-8 reserved
    NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3 = 9,
    NEXUS_CHANNEL_OM_COMMAND_TYPE_INVALID = 255,
};

/*! \enum nexus_channel_om_generic_controller_action_type
 *
 * \brief 'Subtype' used if command_type is `GENERIC_CONTROLLER_ACTION`.
 */
enum nexus_channel_om_generic_controller_action_type
{
    // delete all accessory links from the receiving controller
    NEXUS_CHANNEL_ORIGIN_COMMAND_UNLINK_ALL_LINKED_ACCESSORIES = 0,
    // Unlock all accessories linked to the receiving controller
    NEXUS_CHANNEL_ORIGIN_COMMAND_UNLOCK_ALL_LINKED_ACCESSORIES = 1,
    // types 0-20 reserved
};

/*! \union nexus_channel_om_auth
 *
 * \brief Possible authentication field types
 */
union nexus_channel_om_auth_field {
    uint32_t six_int_digits;
};

struct nexus_channel_om_controller_action_body
{
    uint32_t action_type; // value from
    // `nexus_channel_om_generic_controller_action_type`
};

/* Contains 1 or more ID digits partially identifying a Nexus accessory device
 *
 * For example, if 'id_digits_count' = 3, truncated_id is guaranteed to be
 * between 100 and 999 (inclusive).
 *
 * `truncated_id` is the *least significant* digits of the accessory `nx_id`
 * when read as base-10.
 */
struct nexus_channel_om_truncated_accessory_id
{
    uint32_t digits_int;
    uint8_t digits_count; // how many valid digits
};

struct nexus_channel_om_accessory_action_body
{
    struct nexus_channel_om_truncated_accessory_id trunc_acc_id;
    struct nx_id computed_accessory_id; // inferred field
};

struct nexus_channel_om_create_link_body
{
    struct nexus_channel_om_truncated_accessory_id trunc_acc_id;
    // passed onward to the accessory which will validate it
    union nexus_channel_om_auth_field accessory_challenge;
};

/*! \union nexus_channel_om_command_body
 *
 * \brief Common 'command body' understood by Channel Core.
 *
 * See also: `nexus_channel_om_command_message`
 */
union nexus_channel_om_command_body {
    struct nexus_channel_om_controller_action_body controller_action;
    struct nexus_channel_om_accessory_action_body accessory_action;
    struct nexus_channel_om_create_link_body create_link;
};

/*! \struct nexus_channel_om_command_message
 *
 * \brief Interface between Channel Origin Messaging and Channel Core.
 *
 * This does not represent the actual transmitted contents, but also includes
 * data (possibly in the body, and always in the computed_command_id) which
 * is inferred while parsing and validating the message.
 */
struct nexus_channel_om_command_message
{
    enum nexus_channel_om_command_type type;
    union nexus_channel_om_command_body body;
    union nexus_channel_om_auth_field auth;
    uint32_t computed_command_id; // inferred field, not transmitted in message
};

/*! \brief Initialize Channel Origin Manager state
 *
 * Loads parameters from NV (if present) and initializes state variables.
 *
 * \return void
 */
void nexus_channel_om_init(void);

// expose certain functions for unit tests
#ifdef NEXUS_INTERNAL_IMPL_NON_STATIC

enum nexus_channel_om_command_type
_nexus_channel_om_ascii_validate_command_type(const uint8_t type_int);

bool _nexus_channel_om_ascii_parse_message(
    struct nexus_digits* command_digits,
    struct nexus_channel_om_command_message* message);

/*! \brief Compute auth/check value for Origin "ASCII Digits" message
 *
 * Given a message (with all inferred fields 'filled in') and a symmetric
 * origin key, compute the 6 digits that make up the 'auth' field for this
 * origin command.
 *
 * Does not infer the computed ID, does infer 'inner' body fields if
 * present (and the auth field to account for finding those inner inferred
 * fields) so it may mutate the message.
 *
 * Typically called by `nexus_channel_om_ascii_infer_command_id_compute_auth`,
 * not directly.
 *
 * \param message compute auth value for this message. Message may be mutated
 * \param origin_key key to use when computing auth
 * \return True if successfully inferred fields and auth is valid, else false
 */
bool _nexus_channel_om_ascii_message_infer_inner_compute_auth(
    struct nexus_channel_om_command_message* message,
    const struct nx_core_check_key* origin_key);

/*! \brief Determine command ID and validate auth field for a message
 *
 * Given a message without a known `computed command ID`, loop through all
 * viable command IDs, and determine if any results in a valid message. If so,
 * set the message to that command ID, and confirm that the authentication
 * transmitted in the message matches the calculated authentication field.
 *
 * Will modify the contents of `message`, will not modify the contents of
 * `window`. The caller must update the NV (if any) backing the data
 * represented by `window`.
 *
 * \param message message to authenticate
 * \param window command ID window to use during validation
 * \param origin_key key to use when computing auth
 * \return True if message is valid and authenticates, else false
 */
bool _nexus_channel_om_ascii_infer_fields_compute_auth(
    struct nexus_channel_om_command_message* message,
    const struct nexus_window* window,
    const struct nx_core_check_key* origin_key);

// called by 'parse' to apply the message (if possible), modifying state
bool _nexus_channel_om_ascii_apply_message(
    struct nexus_channel_om_command_message* message);

bool _nexus_channel_om_handle_ascii_origin_command(
    const char* command_data, const uint32_t command_length);

bool _nexus_channel_om_is_command_index_set(uint32_t command_index);
bool _nexus_channel_om_is_command_index_in_window(uint32_t command_index);

#endif /* NEXUS_INTERNAL_IMPL_NON_STATIC */

#endif /* if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE */
#endif /* if NEXUS_CHANNEL_ENABLED */
#endif /* end of include guard: NEXUS__SRC__CHANNEL__CHANNEL_OM_H_ */
