/** \file
 * Nexus Channel Link Handshake OCF Resource (Header)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all
 * copies or substantial portions of the Software.
 *
 * This resource is defined by
 * `ocf_resource_models/NexusChannelLinkHandshakeResURI.swagger.yaml`
 */

#ifndef NEXUS__CHANNEL__SRC__NEXUS_CHANNEL_RES_LINK_HS_H_
#define NEXUS__CHANNEL__SRC__NEXUS_CHANNEL_RES_LINK_HS_H_

#include "src/internal_channel_config.h"
#include "src/nexus_channel_om.h"
#include "src/nexus_util.h"

#if NEXUS_CHANNEL_ENABLED

// accessory will wait this long for controller to finish handshake
#define NEXUS_CHANNEL_LINK_HANDSHAKE_ACCESSORY_TIMEOUT_SECONDS 300

// controller will keep trying to reach an accessory and complete a handshake
// for this long. Allows user time to connect accessory before timeout.
// (1 hour)
#define NEXUS_CHANNEL_LINK_HANDSHAKE_CONTROLLER_TIMEOUT_SECONDS 3600

// retry every 5 seconds until getting a response
#define NEXUS_CHANNEL_LINK_HANDSHAKE_CONTROLLER_RETRY_SECONDS 5

#define NEXUS_CHANNEL_LINK_MAX_CHAL_DATA_BYTES 16
#define NEXUS_CHANNEL_LINK_MAX_RESP_DATA_BYTES 16

#define CHALLENGE_MODE_3_SALT_LENGTH_BYTES 8

// Exposed only for unit tests to confirm resource model contents
#ifdef NEXUS_DEFINED_DURING_TESTING
extern const char* CHAL_DATA_SHORT_PROP_NAME;
extern const char* RESP_DATA_SHORT_PROP_NAME;
extern const char* CHAL_MODE_SHORT_PROP_NAME;
extern const char* LINK_SEC_MODE_SHORT_PROP_NAME;
extern const char* STATE_SHORT_PROP_NAME;
extern const char* TIME_SINCE_INIT_SHORT_PROP_NAME;
extern const char* TIMEOUT_CONFIGURED_SHORT_PROP_NAME;
extern const char* SUPPORTED_LINK_SECURITY_MODES_SHORT_PROP_NAME;
extern const char* SUPPORTED_CHALLENGE_MODES_SHORT_PROP_NAME;
#endif

/*! \enum nexus_channel_link_handshake_challenge_mode
 *
 * Recognized types of link handshake challenge modes. Mode 0 must be
 * supported.
 */
enum nexus_channel_link_handshake_challenge_mode
{
    NEXUS_CHANNEL_LINK_HANDSHAKE_CHALLENGE_MODE_0_CHALLENGE_RESULT = 0,
    NEXUS_CHANNEL_LINK_HANDSHAKE_CHALLENGE_MODE_1_PK0 = 1,
    NEXUS_CHANNEL_LINK_HANDSHAKE_CHALLENGE_MODE_2_CRT1 = 2,
};

enum nexus_channel_link_handshake_state
{
    LINK_HANDSHAKE_STATE_IDLE = 0, // no activity required
    LINK_HANDSHAKE_STATE_ACTIVE = 1, // waiting for response from other device
};

// accessory/server uses to manage link handshake endpoint state
typedef struct nexus_link_hs_accessory_t
{
    // treated as bytestring
    uint8_t chal_data[NEXUS_CHANNEL_LINK_MAX_CHAL_DATA_BYTES];

    // treated as bytestring
    uint8_t resp_data[NEXUS_CHANNEL_LINK_MAX_RESP_DATA_BYTES];
    uint8_t chal_data_len;
    uint8_t resp_data_len;
    enum nexus_channel_link_handshake_challenge_mode chal_mode;
    uint16_t seconds_since_init;
    enum nexus_channel_link_security_mode link_security_mode;
    enum nexus_channel_link_handshake_state state;
} nexus_link_hs_accessory_t;

// Represents one in-progress handshake from the client/controller perspective
// and the MAC should be delegated up to security manager based on that link
typedef struct nexus_link_hs_controller_state_t
{
    struct nx_core_check_key link_key;

    // computed challenge data that is sent when initiating a link
    uint8_t send_chal_data[NEXUS_CHANNEL_LINK_MAX_CHAL_DATA_BYTES];
    uint8_t send_chal_data_len;
    // In the future, salt and salt mac may move into a different module
    // or struct, but for simplicity with one challenge mode, keep them here.
    struct nexus_check_value salt_mac; // MAC computed with key over the salt
    uint8_t salt[CHALLENGE_MODE_3_SALT_LENGTH_BYTES];

    uint16_t seconds_since_init;
    uint16_t last_post_seconds; // used for retries
    enum nexus_channel_link_security_mode requested_security_mode;
    enum nexus_channel_link_handshake_challenge_mode requested_chal_mode;
    enum nexus_channel_link_handshake_state state;
} nexus_link_hs_controller_t;

/** Initialize the Link Handshake resource.
 *
 * Called on startup by `nexus_channel_core_init`.
 */
void nexus_channel_res_link_hs_init(void);

/* Process any pending tasks for Link Handshake module.
 *
 * Handles retries and timeouts. Called within `nexus_channel_core`.
 *
 * \param seconds_elapsed seconds since last call
 * \return seconds until next call
 */
uint32_t nexus_channel_res_link_hs_process(uint32_t seconds_elapsed);

#if NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE
/** GET handler for incoming requests (accessory/server).
 */
void nexus_channel_res_link_hs_server_get(oc_request_t* request,
                                          oc_interface_mask_t if_mask,
                                          void* data);

/** POST handler for incoming requests (accessory/server).
 */
void nexus_channel_res_link_hs_server_post(oc_request_t* request,
                                           oc_interface_mask_t if_mask,
                                           void* data);
#endif /* if NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE */

#if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE
/** Attempt to create a link (challenge mode 3) with an accessory.
 *
 * The process of creating the link is not instantaneous and requires
 * communication with the accessory. Calling this function will begin
 * the process of setting up the link, and return false if unable to
 * begin a link with the given accessory.
 *
 * \param om_body link challenge information for the accessory to link
 * \return true if link establishment has begun, false otherwise
 */
bool nexus_channel_res_link_hs_link_mode_3(
    const struct nexus_channel_om_create_link_body* om_body);

/** Handler for responses to GET requests (as client).
 * XXX This may be implemented in the future, if GET requests
 * for handshakes are required.
 */
void nexus_channel_res_link_hs_client_get(oc_client_response_t* data);

/** Handler for responses to POST requests (as client).
 */
void nexus_channel_res_link_hs_client_post(oc_client_response_t* data);
#endif /* if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE */

#ifdef NEXUS_INTERNAL_IMPL_NON_STATIC
struct nx_core_check_key _res_link_hs_generate_link_key(
    uint32_t challenge_int,
    const uint8_t* salt_bytes,
    uint8_t salt_len,
    const struct nx_core_check_key* derivation_key_a,
    const struct nx_core_check_key* derivation_key_b);

// return current handshake window representation
void _nexus_channel_res_link_hs_get_current_window(struct nexus_window* window);

/* Find correct accessory handshake count for given challenge/handshake.
 *
 * Given handshake data for mode 0, validate the challenge data.
 *
 * For challenge mode 0, takes the salt from the received challenge data, and
 * iterate through handshake counts, generating link keys using the known
 * key derivation formula (using the handshake count and salt as input).
 *
 * For each key, attempt to compute a MAC over the salt. If the MAC matches the
 * MAC which was transmitted in the challenge data with this salt, return true
 * and update the correct handshake/challenge count via reference.
 *
 * `derived_key` points to a struct which will be populated with the key
 * used to validate the challenge if validation was successful.
 *
 * \param salt salt to check. Required 8 bytes in length.
 * \param rcvd_mac valid Siphash 2-4 MAC over the salt.
 * \param window ID window to search for a match (ignoring duplicates)
 * \param matched_handshake_index populated if a match is found
 * \param derived_key will be populated with key if validated
 * \return true if a match is found within the window, false otherwise
 */
bool _nexus_channel_res_link_hs_server_validate_challenge(
    const uint8_t* salt,
    const struct nexus_check_value* rcvd_mac,
    const struct nexus_window* window,
    uint32_t* matched_handshake_index,
    struct nx_core_check_key* derived_key);

// used in unit tests to programmatically set resource state
// (as accessory)
void _nexus_channel_res_link_hs_reset_server_state(void);
void _nexus_channel_res_link_hs_set_server_state(
    const nexus_link_hs_accessory_t* server_state);

void _nexus_channel_res_link_hs_server_post_finalize_success_state(
    const uint32_t matched_handshake_index,
    struct nexus_window* window,
    const struct nx_core_check_key* derived_link_key);

// (as controller)
void _nexus_channel_res_link_hs_set_client_state(
    const nexus_link_hs_controller_t* client_state, uint8_t index);
nexus_link_hs_controller_t*
_nexus_channel_res_link_hs_get_client_state(uint8_t index);
#endif /* ifdef NEXUS_INTERNAL_IMPL_NON_STATIC */

NEXUS_STATIC_ASSERT(
    CHALLENGE_MODE_3_SALT_LENGTH_BYTES == 8,
    "Expected 8 bytes, may need to adjust rounds for correct operation.");

NEXUS_STATIC_ASSERT(CHALLENGE_MODE_3_SALT_LENGTH_BYTES % sizeof(uint32_t) == 0,
                    "Number of salt bytes is not evenly divisble by the number "
                    "of bytes in a uint32, may need to adjust salt "
                    "computation");

#endif /* if NEXUS_CHANNEL_ENABLED */
#endif /* ifndef NEXUS__CHANNEL__SRC__NEXUS_CHANNEL_RES_LINK_HS_H_ */
