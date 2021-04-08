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

    #include "oc/include/nexus_channel_security.h" // functions used only by OC, implemented here
    #include "oc/include/oc_api.h"
    #include "src/nexus_channel_res_lm.h"
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

        OC_DBG("Adding method %u to resource at address %p", method, resource);
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

/** Parse a CoAP message and determine if the requested resource method
 * is secured by Nexus Channel.
 *
 * \param pkt pointer to CoAP message to check if requesting a secured resource
 * method
 * \return true if the requested resource method is secured by Nexus Channel,
 * false otherwise
 */
static bool _nexus_channel_sm_requested_method_is_secured(coap_packet_t* pkt)
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

struct nexus_check_value _nexus_channel_sm_compute_mac_mode0(
    const struct nexus_security_mode0_cose_mac0_t* const received_mac0,
    struct nexus_channel_link_security_mode0_data* const security_data)
{
    NEXUS_ASSERT(security_data != NULL,
                 "Did not receive valid link security data");

    struct nexus_check_value final_computed_check;

    // Two step computation, to avoid having to copy protected data
    // and payload into a new array (which would increase RAM use)
    // Compute check over payload first
    // (Step 1)
    const struct nexus_check_value computed_payload_check =
        nexus_check_compute(&security_data->sym_key,
                            received_mac0->payload,
                            received_mac0->payload_len);

    PRINT("computed payload check bytes (from message):");
    PRINTbytes(computed_payload_check.bytes, sizeof(struct nexus_check_value));

    // construct:
    // * Nonce
    // * Protected data bytes
    // * Previous check result
    //
    // And compute the check over this new input, use result as the
    // 'final' check value
    const uint8_t FINAL_INPUT_ARRAY_SIZE = 13;
    NEXUS_STATIC_ASSERT(4 + 1 + sizeof(struct nexus_check_value) == 13,
                        "Unexpected check value size..");
    uint8_t final_input[FINAL_INPUT_ARRAY_SIZE];
    // bytes 0-3 = nonce
    memcpy(
        final_input, &received_mac0->protected_header_nonce, sizeof(uint32_t));
    // byte 4 = coap type
    final_input[4] = (uint8_t) received_mac0->protected_header_method;
    // bytes 5-12 = previous check result (note nexus_check_value is packed
    // struct)
    memcpy(&final_input[5],
           &computed_payload_check.bytes[0],
           sizeof(struct nexus_check_value));

    // (Step 2)
    final_computed_check = nexus_check_compute(
        &security_data->sym_key, final_input, FINAL_INPUT_ARRAY_SIZE);

    nexus_secure_memclr(security_data,
                        sizeof(struct nexus_channel_link_security_mode0_data),
                        sizeof(struct nexus_channel_link_security_mode0_data));

    return final_computed_check;
}

static bool
_nexus_channel_sm_parse_cose_mac0_protected(const oc_rep_t* rep,
                                            uint8_t* protected_header_method,
                                            uint32_t* protected_header_nonce)
{
    // XXX parsing here expects strict COSE array ordering, e.g.,
    // if 'protected' is not the first element, the parsing will fail.

    if ((rep->type != OC_REP_BYTE_STRING) ||
        (strncmp(oc_string(rep->name), "p", 1) != 0))
    {
        return false;
    }
    // we have the protected bucket, expect a length-5 bytestring
    // for now, we have a method + nonce in there
    if (oc_string_len(rep->value.string) != 5)
    {
        return false;
    }
    // 5 bytes (length confirmed above)
    uint8_t rep_cpy[5];
    memcpy(rep_cpy, rep->value.string.ptr, 5);

    // Will be replaced with bstr wrapping/unwrapping when moving to COSE
    // compliant struct
    *protected_header_method = rep_cpy[0];
    uint32_t protected_nonce_val = 0;
    memcpy(&protected_nonce_val, &rep_cpy[1], sizeof(uint32_t));
    *protected_header_nonce = protected_nonce_val;
    return true;
}

static bool _nexus_channel_sm_parse_cose_mac0_unprotected(const oc_rep_t* rep,
                                                          uint32_t* kid)
{
    // parse single item expected in the unprotected bucket (expect an object
    // with name 'u')
    if ((rep->type != OC_REP_OBJECT) ||
        (strncmp(oc_string(rep->name), "u", 1) != 0))
    {
        return false;
    }

    // make a new rep object to parse the map
    oc_rep_t* u_rep = rep->value.object;

    // first value should be an integer with key "4" (key ID)
    if (u_rep == NULL || u_rep->type != OC_REP_INT ||
        (strncmp(oc_string(u_rep->name), "4", 1) != 0))
    {
        return false;
    }

    oc_rep_value val = u_rep->value;
    *kid = (uint32_t) val.integer;
    OC_DBG("key ID value is %d", (uint32_t) val.integer);

    // expect no more elements in the map
    const bool expected_length = (u_rep->next == NULL);
    oc_free_rep(u_rep);
    if (!expected_length)
    {
        return false;
    }

    return true;
}

static bool _nexus_channel_sm_parse_cose_mac0_payload(
    const oc_rep_t* rep,
    uint8_t** payload, // pointer to payload_ptr
    uint8_t* payload_len)
{
    if (rep == NULL || rep->type != OC_REP_BYTE_STRING ||
        (strncmp(oc_string(rep->name), "d", 1) != 0))
    {
        return false;
    }

    oc_rep_value val = rep->value;
    char* val_str = oc_string(val.string);
    // Note: oc_string_len returns an unsigned integer
    const uint8_t val_str_len = (uint8_t) oc_string_len(val.string);
    PRINT("payload bytes: ");
    for (uint8_t i = 0; i < val_str_len; i++)
    {
        PRINT("%02x ", (uint8_t) val_str[i]);
    }
    PRINT("\n");
    *payload = (uint8_t*) val_str;
    *payload_len = val_str_len;

    return true;
}

bool _nexus_channel_sm_parse_cose_mac0_mac(const oc_rep_t* rep,
                                           struct nexus_check_value** mac)
{
    if (rep == NULL || rep->type != OC_REP_BYTE_STRING ||
        (strncmp(oc_string(rep->name), "m", 1) != 0))
    {
        return false;
    }
    const oc_rep_value val = rep->value;
    char* val_str = oc_string(val.string);
    // Note: oc_string_len returns an unsigned integer
    const uint8_t val_str_len = (uint8_t) oc_string_len(val.string);
    // in security mode 0, we expect the MAC to be
    // exactly 8 bytes
    if (val_str_len != sizeof(struct nexus_check_value))
    {
        return false;
    }

    PRINT("mac bytes: ");
    for (uint8_t i = 0; i < val_str_len; i++)
    {
        PRINT("%02x ", (uint8_t) val_str[i]);
    }
    PRINT("\n");

    // MAC should be the last element in the rep
    if (rep->next != NULL)
    {
        return false;
    }

    *mac = (struct nexus_check_value*) val_str;
    return true;
}

NEXUS_IMPL_STATIC bool _nexus_channel_sm_parse_cose_mac0(
    const oc_rep_t* rep, nexus_security_mode0_cose_mac0_t* cose_mac0_parsed)
{
    if (rep == NULL || rep->next == NULL)
    {
        return false;
    }

    // attempt to parse protected header ('p')
    bool success = _nexus_channel_sm_parse_cose_mac0_protected(
        rep,
        &cose_mac0_parsed->protected_header_method,
        &cose_mac0_parsed->protected_header_nonce);
    rep = rep->next;
    if (!success || rep == NULL)
    {
        return false;
    }

    // attempt to parse unprotected header ('u')
    success = _nexus_channel_sm_parse_cose_mac0_unprotected(
        rep, &cose_mac0_parsed->kid);
    rep = rep->next;
    if (!success || rep == NULL)
    {
        return false;
    }

    // next value after the unprotected map should be the
    // payload ('d')
    success = _nexus_channel_sm_parse_cose_mac0_payload(
        rep, &cose_mac0_parsed->payload, &cose_mac0_parsed->payload_len);
    rep = rep->next;
    if (!success || rep == NULL)
    {
        return false;
    }

    // last value is the MAC ('m')
    success =
        _nexus_channel_sm_parse_cose_mac0_mac(rep, &cose_mac0_parsed->mac);
    rep = rep->next;
    // don't expect any further rep content
    if (!success || rep != NULL)
    {
        return false;
    }

    return true;
}

NEXUS_IMPL_STATIC void _nexus_channel_sm_repack_no_cose_mac0(
    coap_packet_t* const pkt,
    const nexus_security_mode0_cose_mac0_t* const cose_mac0_parsed)
{
    // XXX may be able to refactor this out of here and oc_ri so it's
    // only called once
    char rep_objects_alloc[OC_MAX_NUM_REP_OBJECTS];
    oc_rep_t rep_objects_pool[OC_MAX_NUM_REP_OBJECTS];
    memset(rep_objects_alloc, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(char));
    memset(rep_objects_pool, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(oc_rep_t));
    struct oc_memb rep_objects = {sizeof(oc_rep_t),
                                  OC_MAX_NUM_REP_OBJECTS,
                                  rep_objects_alloc,
                                  (void*) rep_objects_pool,
                                  0};
    oc_rep_set_pool(&rep_objects);

    // encode the outgoing data to test
    uint8_t enc_data[OC_BLOCK_SIZE] = {0};
    oc_rep_new(enc_data, OC_BLOCK_SIZE);
    oc_rep_begin_root_object();

    // 'payload' in a bstr
    oc_rep_set_byte_string(
        root, d, cose_mac0_parsed->payload, cose_mac0_parsed->payload_len);

    oc_rep_end_root_object();
    size_t enc_size = (size_t) oc_rep_get_encoded_payload_size();

    // Repack message to remove Nexus Channel security header
    coap_set_payload(pkt, enc_data, enc_size);
    coap_set_header_content_format(pkt, APPLICATION_VND_OCF_CBOR);

    return;
}

/** Internal method used to extract COSE_MAC0 data from a CoAP packet.
 *
 * \param pkt pointer to CoAP packet to attempt to parse
 * \param cose_mac0_parsed parsed COSE_MAC0 data from `pkt`
 * \return true if the COSE_MAC0 could be parsed from the packet, false
 * otherwise
 */
NEXUS_IMPL_STATIC bool _nexus_channel_sm_get_cose_mac0_data(
    coap_packet_t* const pkt,
    nexus_security_mode0_cose_mac0_t* const cose_mac0_parsed)
{
    // get the CBOR rep from the packet
    oc_rep_t* rep;
    const uint8_t* payload = NULL;
    // Note: oc_string_len returns an unsigned integer
    const uint8_t payload_len = (uint8_t) coap_get_payload(pkt, &payload);

    bool success = false;

    // XXX have to add this section or we get segfaults after calling
    // `oc_parse_rep` on response messages and attempting to access
    // `rep_iterator`.
    char rep_objects_alloc[OC_MAX_NUM_REP_OBJECTS];
    oc_rep_t rep_objects_pool[OC_MAX_NUM_REP_OBJECTS];
    memset(rep_objects_alloc, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(char));
    memset(rep_objects_pool, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(oc_rep_t));
    struct oc_memb rep_objects = {sizeof(oc_rep_t),
                                  OC_MAX_NUM_REP_OBJECTS,
                                  rep_objects_alloc,
                                  (void*) rep_objects_pool,
                                  0};
    oc_rep_set_pool(&rep_objects);

    // attempt to parse the rep
    if (oc_parse_rep(payload, payload_len, &rep) == 0)
    {
        oc_rep_t rep_iterator;
        // don't modify the initial `rep`, so we can free it later
        memcpy(&rep_iterator, rep, sizeof(oc_rep_t));
        // if rep parsing successful, then attempt to extract COSE_MAC0 data
        success =
            _nexus_channel_sm_parse_cose_mac0(&rep_iterator, cose_mac0_parsed);
    }

    oc_free_rep(rep);
    return success;
}

/** Internal method to authenticate message as per Nexus Channel mode 0.
 *
 * \param pkt pointer to `coap_packet_t` that holds the full CoAP packet. The
 * packet may be modified but its pointer shall not. Specifically, if secured
 * and authenticated, then the payload in the packet will be modified to
 * remove all Nexus Channel security information
 * \param cose_mac0_parsed *received* COSE_MAC0 data to use in authentication
 * \param link_sec_data pointer to security data struct to populate
 * used to authenticate the message
 * \return true if `pkt` is authenticated, false otherwise
 */
NEXUS_IMPL_STATIC bool _nexus_channel_sm_auth_packet_mode0(
    coap_packet_t* const pkt,
    const nexus_security_mode0_cose_mac0_t* const cose_mac0_parsed,
    struct nexus_channel_link_security_mode0_data* const link_sec_data)
{
    NEXUS_ASSERT(cose_mac0_parsed->payload != NULL,
                 "if COSE_MAC0 parsed, payload pointer should not be null!");

    // Compute:
    // pass in endpoint and received COSE_MAC0 data and recompute MAC
    const struct nexus_check_value computed_mac =
        _nexus_channel_sm_compute_mac_mode0(cose_mac0_parsed, link_sec_data);

    // Verify:
    // the message is authenticated if computed MAC matches message MAC
    const bool success = memcmp((const void*) &computed_mac.bytes,
                                (const void*) cose_mac0_parsed->mac,
                                sizeof(struct nexus_check_value)) == 0;

    if (success)
    {
        // Repack message without Nexus Channel security data
        _nexus_channel_sm_repack_no_cose_mac0(pkt, cose_mac0_parsed);
    }
    return success;
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

coap_status_t
nexus_channel_authenticate_message(const oc_endpoint_t* const endpoint,
                                   coap_packet_t* const pkt)
{
    // first, check if the requested resource method is secured
    const bool res_method_secured =
        _nexus_channel_sm_requested_method_is_secured(pkt);

    // check if the message is secured based on CoAP headers
    // XXX: when other security modes are supported, then we can look up the
    // link security mode and conditionally check headers (only for mode 0)
    const bool message_header_secured =
        _nexus_channel_sm_message_headers_secured_mode0(pkt);

    if (message_header_secured)
    {
        // attempt to get encoded COSE_MAC0 data
        nexus_security_mode0_cose_mac0_t cose_mac0_parsed;
        memset(
            &cose_mac0_parsed, 0x00, sizeof(nexus_security_mode0_cose_mac0_t));

        if (!_nexus_channel_sm_get_cose_mac0_data(pkt, &cose_mac0_parsed))
        {
            // return 400 if unable to parse
            return BAD_REQUEST_4_00;
        }

        // convert IPV6 to Nexus ID
        struct nx_id nexus_id;
        nexus_oc_wrapper_oc_endpoint_to_nx_id(endpoint, &nexus_id);

        // get link security data
        struct nexus_channel_link_security_mode0_data cur_mode0_sec_data;
        memset(&cur_mode0_sec_data,
               0x00,
               sizeof(struct nexus_channel_link_security_mode0_data));

        if (!nexus_channel_link_manager_security_data_from_nxid(
                &nexus_id, &cur_mode0_sec_data))
        {
            // return 400 if we can't find a link
            return BAD_REQUEST_4_00;
        }

        // received nonce should be greater than current nonce
        // XXX: overflow risk
        if (cur_mode0_sec_data.nonce > cose_mac0_parsed.protected_header_nonce)
        {
            // clear sensitive security info
            nexus_secure_memclr(
                &cur_mode0_sec_data,
                sizeof(struct nexus_channel_link_security_mode0_data),
                sizeof(struct nexus_channel_link_security_mode0_data));
            // return status code to indicate that nonce is invalid
            return NOT_ACCEPTABLE_4_06;
        }

        // try to authenticate via MAC
        if (!_nexus_channel_sm_auth_packet_mode0(
                pkt, &cose_mac0_parsed, &cur_mode0_sec_data))
        {
            // return 401 if unauthenticated
            nexus_secure_memclr(
                &cur_mode0_sec_data,
                sizeof(struct nexus_channel_link_security_mode0_data),
                sizeof(struct nexus_channel_link_security_mode0_data));
            return UNAUTHORIZED_4_01;
        }

        // if authenticated, update nonce value and indicate link is active
        nexus_channel_link_manager_set_security_data_auth_nonce(
            &nexus_id, cose_mac0_parsed.protected_header_nonce);
        nexus_channel_link_manager_reset_link_secs_since_active(&nexus_id);
        nexus_secure_memclr(
            &cur_mode0_sec_data,
            sizeof(struct nexus_channel_link_security_mode0_data),
            sizeof(struct nexus_channel_link_security_mode0_data));
        return COAP_NO_ERROR;
    }
    else if (res_method_secured)
    {
        // a secured resource method and unsecured message will fail
        // authentication
        return UNAUTHORIZED_4_01;
    }
    // the last case, unsecured resource method and message, requires no
    // authentication

    return COAP_NO_ERROR;
}

    #ifdef NEXUS_DEFINED_DURING_TESTING
uint8_t _nexus_channel_sm_secured_resource_methods_count(void)
{
    // Note: oc_list_length returns an int
    return (uint8_t) oc_list_length(nexus_sec_res_methods);
}
    #endif

#endif /* if NEXUS_CHANNEL_LINK_SECURITY_ENABLED */
