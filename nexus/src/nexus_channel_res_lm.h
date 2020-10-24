/** \file
 * Nexus Channel Link Manager OCF Resource (Header)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all
 * copies or substantial portions of the Software.
 */
#ifndef NEXUS__CHANNEL__SRC__NEXUS_CHANNEL_RES_LM__H
#define NEXUS__CHANNEL__SRC__NEXUS_CHANNEL_RES_LM__H

// also provides definition of nexus_channel_link_security_mode enum
// and nexus_channel_link_operating mode, which are shared by both
// link manager and handshake manager. This prevents circular dependencies
// between link manager and handshake manager.
#include "src/internal_channel_config.h"

#if NEXUS_CHANNEL_LINK_SECURITY_ENABLED

    #ifdef __cplusplus
extern "C" {
    #endif

    // Exposed only for unit tests to confirm resource model contents
    #ifdef NEXUS_DEFINED_DURING_TESTING
extern const char* L_LINKED_DEVICE_ID_SHORT_PROP_NAME;
extern const char* L_CHAL_MODE_SHORT_PROP_NAME;
extern const char* L_LINK_SEC_MODE_SHORT_PROP_NAME;
extern const char* L_TIME_SINCE_INIT_SHORT_PROP_NAME;
extern const char* L_TIME_SINCE_ACTIVITY_SHORT_PROP_NAME;
extern const char* L_TIMEOUT_CONFIGURED_SHORT_PROP_NAME;
    #endif

    #ifndef NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS
        #error "NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS must be defined."
    #endif

/* Security data for link mode 0.
 *
 * Authentication, no encryption.
 *
 * Shared symmetric 128-bit link key is used to compute a MAC
 * using Siphash 2-4, and a nonce is incremented every time a MAC
 * is computed.
 */
NEXUS_PACKED_STRUCT nexus_channel_link_security_mode0_data
{
    // 128-bit symmetric link key
    struct nx_common_check_key sym_key;
    uint32_t nonce;
};

NEXUS_PACKED_UNION nexus_channel_link_security_data
{
    struct nexus_channel_link_security_mode0_data mode0;
};

// Representation of each established Nexus Channel Link.
typedef NEXUS_PACKED_STRUCT nexus_channel_link_t
{
    // these elements are provided via the OCF resource representation
    struct nx_id linked_device_id; // 6 bytes
    uint8_t operating_mode;
    uint8_t security_mode;
    uint32_t seconds_since_init;
    uint32_t seconds_since_active;
    // `tTimeout` is fixed / hardcoded for all links currently

    // These elements are hidden/not exposed in OCF resource
    union nexus_channel_link_security_data security_data;
}
nexus_channel_link_t;

// Fixed size matters since we persist link state in NV
// see also `NX_COMMON_NV_BLOCK_4_LENGTH`
NEXUS_STATIC_ASSERT(
    sizeof(nexus_channel_link_t) == 6 + 1 + 1 + 4 + 4 + 16 + 4, // 36
    "Unexpected size for `nexus_channel_link_t`, NV storage may fail");

/* Initialize the Nexus Channel Link module.
 *
 * Called on startup by `nexus_channel_core_init()`.
 *
 * \return true if initialized successfully, false otherwise
 */
bool nexus_channel_link_manager_init(void);

/* Periodic processing function to update Nexus Channel Link state.
 *
 * Called inside `nexus_channel_core_process()`.
 *
 * This is primarily used to eliminate 'dead' links that have timed out, but
 * may also be used to update any other link-specific parameters that might
 * need to be adjusted synchronously.
 *
 * \param seconds_elapsed number of seconds since last time this was called
 * \return number of seconds until next process call must occur
 */
/* Process any pending tasks for Link Manager module.
 *
 * Handles timeouts. Called within `nexus_channel_core`.
 *
 * \param seconds_elapsed seconds since last call
 * \return seconds until next call
 */
uint32_t nexus_channel_link_manager_process(uint32_t seconds_elapsed);

/* Create a new Nexus Channel link.
 *
 * Provided with the Nexus ID of the device to link to, the link
 * operating mode, link security mode, and security data for the link,
 * create a new Link.
 *
 * This is typically called by Link Handshake manager to establish a new
 * link once the handshake has established security parameters for the link.
 *
 * If the operating mode is not supported, the security mode is not
 * supported, or the security data is invalid, the link will not be created.
 *
 * The link might also not be created if there are already
 * `NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS` established.
 *
 * \return true if link was created successfully, false otherwise
 */
bool nexus_channel_link_manager_create_link(
    const struct nx_id* linked_device_id,
    enum nexus_channel_link_operating_mode operating_mode,
    enum nexus_channel_link_security_mode security_mode,
    const union nexus_channel_link_security_data* security_data);

/* Delete all Nexus Channel Links.
 *
 * Used to 'reset' the entire link state of a Nexus Channel device.
 * Does not reset origin command counter (controller) or link handshake
 * counter (accessory).
 */
void nexus_channel_link_manager_clear_all_links(void);

/* Return the current operating mode of this device.
 *
 * Used by other modules to determine if this device is operating in an
 * accessory or controller mode currently.
 *
 * \return current Nexus Channel mode of this device.
 */
enum nexus_channel_link_operating_mode
nexus_channel_link_manager_operating_mode(void);

/* Return true if this device is linked to another controller device.
 *
 * This implies that another device is capable of controlling this device
 * for resources that depend on a controller/accessory link.
 *
 * Does not guarantee that only one controller is linked - multiple
 * controllers may be present (although this is practically unlikely).
 *
 * This method may need to be extended when/if multiple controllers control
 * a single accessory. The ID of the first linked controller found (if any)
 * will be populated in the struct pointed to by `linked_controller`.
 *
 * \return true if linked to an controller, false otherwise
 */
bool nexus_channel_link_manager_has_linked_controller(void);

/* Return true if this device is linked to another accessory device.
 *
 * \return true if linked to an accessory, false otherwise
 */
bool nexus_channel_link_manager_has_linked_accessory(void);

/* Obtain a Nexus channel link from a Nexus ID.
 *
 * Will look for a link to the referenced Nexus ID, and if present, will
 * will copy the security data from that link into `security_data` and
 * return true.
 *
 * If there is no link found to the specified Nexus ID, this function will
 * return false and `security_data` will be unmodified.
 *
 * Currently supports returning Mode 0 security data.
 *
 * \param nx_id Nexus ID of linked device to find
 * \param security_data Will be populated with security data to use
 * \return true if link exists and security_data is populated, false otherwise
 */
bool nexus_channel_link_manager_security_data_from_nxid(
    const struct nx_id* id,
    struct nexus_channel_link_security_mode0_data* security_data);

/* Set authentication nonce for a given Channel Link.
 *
 * Called when a link is used (typically when sending a request over the
 * link) to increase the counter or nonce used to secure the link. Not
 * expected to be used outside of Security Manager.
 *
 * WARNING: This method will always set the nonce to the requested value;
 * the caller is responsible for checking that the new value is valid!
 *
 * \param id Nexus ID of link to increment security data nonce for
 * \param new_nonce value to set nonce to
 * \return true if nonce was set to `new_nonce`, false otherwise
 */
bool nexus_channel_link_manager_set_security_data_auth_nonce(
    const struct nx_id* id, uint32_t new_nonce);

/** Reset `seconds_since_active` for a Nexus Channel link.
 *
 * Called when a link is used (successfully receiving or sending a message).
 * Not expected to be used outside of security manager.
 *
 * \param id Nexus ID of link to reset `seconds_since_active`
 * \return true if reset, false otherwise
 */
bool nexus_channel_link_manager_reset_link_secs_since_active(
    const struct nx_id* id);

    #ifdef NEXUS_INTERNAL_IMPL_NON_STATIC
// Used internally, will retrieve an entire link entity given the NXID.
bool _nexus_channel_link_manager_link_from_nxid(
    const struct nx_id* id, nexus_channel_link_t* retrieved_link);
bool _nexus_channel_link_manager_index_to_nv_block(
    uint8_t index, struct nx_common_nv_block_meta** dest_block_meta_ptr);
    #endif // NEXUS_INTERNAL_IMPL_NON_STATIC

void nexus_channel_res_lm_server_get(oc_request_t* request,
                                     oc_interface_mask_t if_mask,
                                     void* data);

    // Only defined for unit tests
    #ifdef NEXUS_DEFINED_DURING_TESTING
// get `secs_since_active` for link of the given Nexus ID
uint32_t
_nexus_channel_link_manager_secs_since_link_active(const struct nx_id* id);
    #endif

    #ifdef __cplusplus
}
    #endif

#endif // NEXUS_CHANNEL_LINK_SECURITY_ENABLED

#endif // NEXUS__CHANNEL__SRC__NEXUS_CHANNEL_RES_LM__H
