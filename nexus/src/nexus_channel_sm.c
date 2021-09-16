/** \file nexus_channel_sm.c
 * Nexus Channel Security Manager Module (Implementation)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "src/nexus_channel_sm.h"

#if NEXUS_CHANNEL_LINK_SECURITY_ENABLED

    #include "oc/include/oc_api.h"
    #include "src/nexus_channel_res_lm.h"
    #include "src/nexus_cose_mac0_sign.h"
    #include "src/nexus_cose_mac0_verify.h"
    #include "src/nexus_oc_wrapper.h"
    #include "src/nexus_security.h"
    #include "utils/oc_list.h"

// Create statically-allocated memory block to store resource method security
// configs and list to manage them. Each resource may up to have 4 methods.
// Note: Constrained to two methods (GET and POST)
OC_MEMB(nexus_sec_res_methods_memb,
        nexus_secured_resource_method_t,
        OC_MAX_APP_RESOURCES * 2);
OC_LIST(nexus_sec_res_methods);

nexus_secured_resource_method_t*
nexus_channel_sm_nexus_resource_method_new(oc_resource_t* resource,
                                           oc_method_t method)
{
    nexus_secured_resource_method_t* nexus_sec_res_method =
        (nexus_secured_resource_method_t*) oc_memb_alloc(
            &nexus_sec_res_methods_memb);
    if (nexus_sec_res_method)
    {
        nexus_sec_res_method->resource = resource;
        nexus_sec_res_method->method = method;

        OC_DBG("Adding method %u to resource at address %p",
               method,
               (void*) resource);
        oc_list_add(nexus_sec_res_methods, nexus_sec_res_method);
    }
    else
    {
        oc_memb_free(&nexus_sec_res_methods_memb, nexus_sec_res_method);
        nexus_sec_res_method = NULL;
        OC_WRN("insufficient memory to store Nexus Resource Method");
    }

    return nexus_sec_res_method;
}

void nexus_channel_sm_init(void)
{
    oc_list_init(nexus_sec_res_methods);
    oc_memb_init(&nexus_sec_res_methods_memb);
    return;
}

bool nexus_channel_sm_resource_method_is_secured(
    const oc_resource_t* const resource, const oc_method_t method)
{
    if (resource == NULL)
    {
        return false;
    }

    const nexus_secured_resource_method_t* nexus_sec_res_method =
        (nexus_secured_resource_method_t*) oc_list_head(nexus_sec_res_methods);
    while (nexus_sec_res_method != NULL)
    {
        if (nexus_sec_res_method->resource &&
            strncmp(oc_string(nexus_sec_res_method->resource->uri),
                    oc_string(resource->uri),
                    oc_string_len(resource->uri)) == 0 &&
            (nexus_sec_res_method->method == method))
        {
            return true;
        }
        nexus_sec_res_method = nexus_sec_res_method->next;
    }

    /** It's possible that when we return false here, the resource or resource
     * method simply does not exist; in this case, we will return an
     * appropriate failure message later down in the message processing chain
     */
    return false;
}

void nexus_channel_sm_free_all_nexus_resource_methods(void)
{
    nexus_secured_resource_method_t* nexus_sec_res_method =
        (nexus_secured_resource_method_t*) oc_list_head(nexus_sec_res_methods);
    nexus_secured_resource_method_t* next = nexus_sec_res_method;
    while (next != NULL)
    {
        next = nexus_sec_res_method->next;
        oc_list_remove(nexus_sec_res_methods, nexus_sec_res_method);
        oc_memb_free(&nexus_sec_res_methods_memb, nexus_sec_res_method);
        nexus_sec_res_method = next;
    }
}

bool nexus_channel_sm_requested_method_is_secured(
    const coap_packet_t* const pkt)
{
    // determine if resource method is secured by Nexus Channel
    const char* href;
    const size_t href_len = coap_get_header_uri_path((void*) pkt, &href);
    if (href_len == 0)
    {
        return false;
    }
    const oc_resource_t* const resource = oc_ri_get_app_resource_by_uri(
        href, href_len, NEXUS_CHANNEL_NEXUS_DEVICE_ID);
    const oc_method_t method = (oc_method_t) pkt->code;
    return nexus_channel_sm_resource_method_is_secured(resource, method);
}

NEXUS_IMPL_STATIC bool
_nexus_channel_sm_message_headers_secured_mode0(coap_packet_t* const pkt)
{
    unsigned int format = 0;
    coap_get_header_content_format((void*) pkt, &format);
    if (format == APPLICATION_COSE_MAC0)
    {
        return true;
    }

    // APPLICATION_VND_OCF_CBOR and any other type
    return false;
}

nexus_channel_sm_auth_error_t
nexus_channel_authenticate_message(const oc_endpoint_t* const endpoint,
                                   coap_packet_t* const pkt)
{
    // first, check if the requested resource method is secured
    const bool res_method_secured =
        nexus_channel_sm_requested_method_is_secured(pkt);

    // check if the message is secured based on CoAP headers
    // Note: when other security modes are supported, then we can look up the
    // link security mode and conditionally check headers (only for mode 0)
    const bool message_header_secured =
        _nexus_channel_sm_message_headers_secured_mode0(pkt);

    #ifdef NEXUS_USE_DEFAULT_ASSERT
    size_t original_payload_len = pkt->payload_len;
    #endif

    nexus_channel_sm_auth_error_t sm_auth_result =
        NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_NONE;

    // message headers indicates it is secured with COSE MAC0 (used
    // by security mode0), we'll attempt to extract and authenticate
    // the secured payload here.
    if (message_header_secured)
    {
        uint32_t received_nonce;

        // convert IPV6 to Nexus ID to get security data
        struct nx_id nexus_id;
        nexus_oc_wrapper_oc_endpoint_to_nx_id(endpoint, &nexus_id);

        if (pkt->payload_len > NEXUS_CHANNEL_MAX_CBOR_PAYLOAD_SIZE)
        {
            // unable to parse - incoming payload is larger than supported
            return NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_PAYLOAD_SIZE_INVALID;
        }
        else if (pkt->payload_len == 0)
        {
            // should never receive a secured payload with a length of 0
            return NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_PAYLOAD_SIZE_INVALID;
        }

        // get link security data
        struct nexus_channel_link_security_mode0_data link_security_data;
        if (!nexus_channel_link_manager_security_data_from_nxid(
                &nexus_id, &link_security_data))
        {
            return NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_SENDER_DEVICE_NOT_LINKED;
        }

        const nexus_cose_mac0_verify_ctx_t verify_ctx = {
            // link key,
            &link_security_data.sym_key,
            // aad
            {
                pkt->code, // request method
                (uint8_t*) pkt->uri_path,
                (uint8_t) pkt->uri_path_len,
            },
            pkt->payload,
            pkt->payload_len,
        };

        // checks that the message MAC is not tampered and is valid
        // against itself, using the link key and message contents. Does
        // *not* check whether the nonce is too low or not.
        size_t unsecured_payload_len = 0;
        nexus_cose_error verify_result =
            nexus_cose_mac0_verify_message(&verify_ctx,
                                           &received_nonce,
                                           &pkt->payload,
                                           &unsecured_payload_len);
        pkt->payload_len = (uint32_t) unsecured_payload_len;

        if (verify_result == NEXUS_COSE_ERROR_MAC_TAG_INVALID)
        {
            OC_DBG("COSE MAC/tag invalid. Received nonce %d", received_nonce);
            sm_auth_result = NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_MAC_INVALID;
        }
        else if (verify_result != NEXUS_COSE_ERROR_NONE)
        {
            // return 400 if unable to parse or any other error except bad MAC
            OC_WRN("Attempted to verify unparseable message");
            sm_auth_result =
                NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_COSE_UNPARSEABLE;
        }
        // Otherwise, the message received is internally consistent.
        //
        // Handle request messages
        else if (pkt->code < CREATED_2_01)
        {
            // We are approaching rollover of the nonce - reset it to 0.
            // This is done by sending a NONCE SYNC with a value of UINT32_MAX
            // which is interpreted to mean 'we've reset nonce to 0'.
            NEXUS_STATIC_ASSERT(
                NEXUS_CHANNEL_LINK_SECURITY_NONCE_NV_STORAGE_INTERVAL_COUNT >=
                    16,
                "Nonce NV interval too small, expected at least 16");

            // requests must have a nonce > the current nonce
            if (received_nonce < (link_security_data.nonce + 1))
            {
                sm_auth_result =
                    NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_REQUEST_RECEIVED_WITH_INVALID_NONCE;
            }
            else if (
                received_nonce >
                (UINT32_MAX -
                 NEXUS_CHANNEL_LINK_SECURITY_NONCE_NV_STORAGE_INTERVAL_COUNT))
            {
                // if MAC is valid and nonce is an acceptable value, update our
                // local nonce value and indicate that the link is active if
                // authenticated, update nonce value and indicate link is active
                nexus_channel_link_manager_set_security_data_auth_nonce(
                    &nexus_id, 0);
                nexus_channel_link_manager_reset_link_secs_since_active(
                    &nexus_id);
                sm_auth_result =
                    NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_NONCE_APPROACHING_MAX_FORCED_RESET_REQUIRED;
            }
        }
        // response messages (pkt->code >= CREATED_2_01)
        else
        {
            NEXUS_ASSERT(pkt->code >= CREATED_2_01,
                         "Unexpectedly handling non-response message");
            // Any response message must have a nonce at least equal to the
            // current nonce on this device
            if (received_nonce < link_security_data.nonce)
            {
                sm_auth_result =
                    NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_REQUEST_RECEIVED_WITH_INVALID_NONCE;
            }
            // received a nonce-sync message with a valid nonce...
            else if (pkt->code == NOT_ACCEPTABLE_4_06)
            {
                sm_auth_result =
                    NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_VALID_NONCE_SYNC_RECEIVED;
                uint32_t nonce_to_sync = received_nonce;
                if (received_nonce ==
                    NEXUS_CHANNEL_LINK_SECURITY_RESET_NONCE_SIGNAL_VALUE)
                {
                    // special case - reset local nonce to 0
                    nonce_to_sync = 0;
                }
                // update the local notion of the nonce
                nexus_channel_link_manager_set_security_data_auth_nonce(
                    &nexus_id, nonce_to_sync);
                nexus_channel_link_manager_reset_link_secs_since_active(
                    &nexus_id);
            }
        }

        // clear sensitive security info
        nexus_secure_memclr(
            &link_security_data,
            sizeof(struct nexus_channel_link_security_mode0_data),
            sizeof(struct nexus_channel_link_security_mode0_data));

        if (sm_auth_result != NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_NONE)
        {
            return sm_auth_result;
        }

        // if MAC is valid and nonce is an acceptable value, update our local
        // nonce value and indicate that the link is active
        // if authenticated, update nonce value and indicate link is active
        nexus_channel_link_manager_set_security_data_auth_nonce(&nexus_id,
                                                                received_nonce);
        nexus_channel_link_manager_reset_link_secs_since_active(&nexus_id);
        // Overwrite the secured payload with the unsecured payload, which
        // we know must be equal or shorter in length from checks earlier
        // in this function
    #ifdef NEXUS_USE_DEFAULT_ASSERT
        NEXUS_ASSERT(
            original_payload_len >= pkt->payload_len,
            "Secured payload unexpectedly has smaller payload than unsecured");
    #endif
        NEXUS_ASSERT(sm_auth_result == NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_NONE,
                     "Updated CoAP payload pointer but auth result was error");
        return sm_auth_result;
    }
    else if (res_method_secured)
    {
        // a secured resource method and unsecured message will fail
        // authentication
        return NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_RESOURCE_REQUIRES_SECURED_REQUEST;
    }
    // the last case, unsecured resource method and message, requires no
    // authentication

    return NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_NONE;
}

    #ifdef NEXUS_DEFINED_DURING_TESTING
uint8_t _nexus_channel_sm_secured_resource_methods_count(void)
{
    // Note: oc_list_length returns an int
    return (uint8_t) oc_list_length(nexus_sec_res_methods);
}
    #endif

#endif /* if NEXUS_CHANNEL_LINK_SECURITY_ENABLED */
