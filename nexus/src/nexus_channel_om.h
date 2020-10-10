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

#if NEXUS_CHANNEL_LINK_SECURITY_ENABLED
    // Origin commands are only relevant for a Channel Controller
    #if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE
        #define NEXUS_CHANNEL_OM_COMMAND_ASCII_DIGITS_MAX_LENGTH 20
        #define NEXUS_CHANNEL_OM_COMMAND_ASCII_MAX_BYTES_TO_AUTH 15
        #define NEXUS_CHANNEL_OM_INVALID_COMMAND_COUNT 0xFFFFFFFF // sentinel

        #ifdef __cplusplus
extern "C" {
        #endif

/*! \brief Initialize Channel Origin Manager state
 *
 * Loads parameters from NV (if present) and initializes state variables.
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
 * Typically called by
 * `nexus_channel_om_ascii_infer_command_id_compute_auth`, not directly.
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
 * viable command IDs, and determine if any results in a valid message. If
 * so, set the message to that command ID, and confirm that the
 * authentication transmitted in the message matches the calculated
 * authentication field.
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

    #ifdef __cplusplus
}
    #endif

#endif /* if NEXUS_CHANNEL_LINK_SECURITY_ENABLED */
#endif /* end of include guard: NEXUS__SRC__CHANNEL__CHANNEL_OM_H_ */
