/** \file
 * Nexus Channel Security Manager Module (Header)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#ifndef NEXUS__CHANNEL__SRC__NEXUS_CHANNEL_SM__H
#define NEXUS__CHANNEL__SRC__NEXUS_CHANNEL_SM__H

#include "src/internal_channel_config.h"

#if NEXUS_CHANNEL_LINK_SECURITY_ENABLED

    #include "oc/include/oc_ri.h"
    #include "oc/messaging/coap/coap.h"
    #include "src/nexus_channel_core.h"
    #include "src/nexus_channel_res_lm.h"
    #include "src/nexus_util.h"

    #ifdef __cplusplus
extern "C" {
    #endif

    // Used when performing a nonce-sync to signal that nonce should be
    // reset to 0.
    #define NEXUS_CHANNEL_LINK_SECURITY_RESET_NONCE_SIGNAL_VALUE UINT32_MAX

// struct for storing Nexus Channel-secured resource methods
typedef struct nexus_secured_resource_method_t
{
    struct nexus_secured_resource_method_t* next;
    oc_resource_t* resource;
    oc_method_t method;
} nexus_secured_resource_method_t;

/** Initialize the Nexus Channel Security Manager module.
 *
 * Called on startup by `nexus_channel_core_init`.
 */
void nexus_channel_sm_init(void);

/** Return whether a given resource method is secured by Nexus Channel.
 * Any resource method that was registered with the `secured` parameter as
 * true in `nexus_channel_sm_set_request_handler` or
 * `nx_channel_register_resource(_handler)` should return true from this
 * function.
 *
 * \param resource pointer to the `oc_resource_t` to check if secured
 * \param method method ('oc_method_t`) of the resource to check if secured
 * \return true if the resource method is secured by Nexus Channel, false
 * otherwise
 */
bool nexus_channel_sm_resource_method_is_secured(
    const oc_resource_t* const resource, const oc_method_t method);

/** Create a new Nexus Channel resource method, including security
 * configuration.
 *
 * \param resource pointer to the `oc_resource_t` to register security
 * \param method method (`oc_method_t`) of the resource to register
 * security
 * \return the `nexus_secured_resource_method_t` object created, or null if
 * unable to register the security
 */
nexus_secured_resource_method_t*
nexus_channel_sm_nexus_resource_method_new(oc_resource_t* resource,
                                           oc_method_t method);

/** Free memory occupied by secured Nexus Channel resource methods.
 */
void nexus_channel_sm_free_all_nexus_resource_methods(void);

    // Expose for unit tests only
    #ifdef NEXUS_INTERNAL_IMPL_NON_STATIC
/** Check if Nexus Channel headers indicate that this CoAP packet is secured
 * with Nexus Channel.
 *
 * \param pkt pointer to `coap_packet_t` of the CoAP packet to check for Nexus
 * Channel security
 * \return true if according to the headers, the CoAP message is secured
 * by Nexus Channel. False otherwise
 */
bool _nexus_channel_sm_message_headers_secured_mode0(coap_packet_t* const pkt);
    #endif

/** Parse a CoAP message and determine if the requested resource method
 * is secured by Nexus Channel.
 *
 * \param pkt pointer to CoAP message to check if requesting a secured resource
 * method
 * \return true if the requested resource method is secured by Nexus Channel,
 * false otherwise
 */
bool nexus_channel_sm_requested_method_is_secured(
    const coap_packet_t* const pkt);

/* Result of attempting to parse and authenticate an incoming message.
 *
 * Unsecured CoAP packets will result in a short-circuit return value of
 * `NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_NONE`. Secured CoAP packets
 * will have their content-header, payload pointer, and payload length modified
 * to appear as an unsecured message to the calling code, and if there are
 * no authentication errors, will cause a return value of
 * `NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_NONE`.
 */
typedef enum
{
    // No error - pass CoAP packet to application request/response handler.
    NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_NONE,
    // Valid secured payload format, but security MAC/tag invalid
    NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_MAC_INVALID,
    // Error parsing the COSE structure from a secured payload
    NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_COSE_UNPARSEABLE,
    // Payload is too large or too small to process
    NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_PAYLOAD_SIZE_INVALID,
    // No secured link exists to the device sending the request
    NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_SENDER_DEVICE_NOT_LINKED,
    // Received an unsecured request for a secured resource
    NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_RESOURCE_REQUIRES_SECURED_REQUEST,
    // Received a secured request, but it had an invalid nonce
    NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_REQUEST_RECEIVED_WITH_INVALID_NONCE,
    // should trigger a resend of the previous secured request
    NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_VALID_NONCE_SYNC_RECEIVED,
    // approaching max possible nonce value, trigger a reset to 0
    NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_NONCE_APPROACHING_MAX_FORCED_RESET_REQUIRED,

} nexus_channel_sm_auth_error_t;

/** Authenticate message against Nexus Channel security.
 *
 * If the message contains Nexus security information, then that security
 * information will be checked against currently active Nexus links. If
 * the message is unsecured, then it will only pass authentication if it
 * is bound for an unsecured resource method.
 *
 * The method will return `NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_NONE` if the
 * received message in `pkt` should be passed on to an appropriate application
 * resource handler.
 *
 * Returns `NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_VALID_NONCE_SYNC_RECEIVED`
 * if the received message was a 'nonce sync' response and the link nonce
 * has been updated, and the application should resend the orignal secured
 * request with a matching token/mid with updated security data.
 *
 * \param endpoint pointer to `oc_endpoint_t` representing the endpoint of the
 * received CoAP message. Neither pointer nor value shall be modified
 * \param pkt pointer to the `coap_packet_t` that contains the received CoAP
 * message information. The pointer shall not be modified but the packet
 * itself may be changed for a secured and authenticated message
 * \return error code representing Nexus Channel authentication check result
 */
nexus_channel_sm_auth_error_t
nexus_channel_authenticate_message(const oc_endpoint_t* const endpoint,
                                   coap_packet_t* const pkt);

    // Only defined for unit tests
    #ifdef NEXUS_DEFINED_DURING_TESTING
uint8_t _nexus_channel_sm_secured_resource_methods_count(void);
    #endif

    #ifdef __cplusplus
}
    #endif

#endif /* if NEXUS_CHANNEL_LINK_SECURITY_ENABLED */
#endif /* ifdef NEXUS__CHANNEL__SRC__NEXUS_CHANNEL_SM__H */
