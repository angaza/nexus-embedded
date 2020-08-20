/** \file
 * Nexus Channel Protocol Internal Configuration Parameters
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#ifndef NEXUS__CHANNEL__SRC__INTERNAL_CHANNEL_CONFIG_H_
#define NEXUS__CHANNEL__SRC__INTERNAL_CHANNEL_CONFIG_H_

// "nxp" will be included independently by modules that them.
#include "include/nx_channel.h"
#include "src/internal_common_config.h"

#include "oc/include/oc_client_state.h"

// Compile-time parameter checks
#ifndef NEXUS_CHANNEL_ENABLED
#error "NEXUS_CHANNEL_ENABLED must be defined"
#endif

// Set up further configuration parameters
#if NEXUS_CHANNEL_ENABLED

// Identifies the Nexus channel protocol public 'release version'.
#define NEXUS_CHANNEL_PROTOCOL_RELEASE_VERSION_COUNT 1

// Not externally exposed, configures number of link handshakes a controller
// can simultaneously have.
#define NEXUS_CHANNEL_SIMULTANEOUS_LINK_HANDSHAKES 4

// maximum number of simultaneous Nexus Channel links that can be established
// Once reaching this limit, devices must be unlinked to link more devices.
// Increasing increases RAM and NV use.
#define NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS                                   \
    CONFIG_NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS

// Seconds that an established link must be idle before being deleted
// from this system. 7776000 = 3 months
#define NEXUS_CHANNEL_LINK_TIMEOUT_SECONDS 7776000

/*
 * Possible ways to secure communication on a Nexus Channel Link.
 *
 * Used by Link Handshake manager and Link manager to set up a new link
 * and manage encryption and authentication on an existing link, respectively.
 */
enum nexus_channel_link_security_mode
{
    // No encryption, 128-bit symmetric key, COSE MAC0 computed w/ Siphash 2-4
    NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24 = 0,
    // 1-3 reserved
};

/*
 * Possible operating modes for a device on Nexus Channel Link.
 *
 * Typically, a device on one end of the link is a 'controller', and the
 * other is an 'accessory'.
 */
enum nexus_channel_link_operating_mode
{
    // operating as an accessory only
    CHANNEL_LINK_OPERATING_MODE_ACCESSORY = 0,
    // operating as a controller only
    CHANNEL_LINK_OPERATING_MODE_CONTROLLER = 1,
    // simultaneous accessory and controller modes
    CHANNEL_LINK_OPERATING_MODE_DUAL_MODE_ACTIVE = 2,
    // Capable of both modes (dual mode), neither active
    CHANNEL_LINK_OPERATING_MODE_DUAL_MODE_IDLE = 3,
};

#if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE
// Shared origin-manager enums and structs (shared by `nexus_channel_om` and
// `nexus_channel_core`

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
#endif /* NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE */

#endif /* NEXUS_CHANNEL_ENABLED */
#endif /* ifndef NEXUS__CHANNEL__SRC__INTERNAL_CHANNEL_CONFIG_H_ */
