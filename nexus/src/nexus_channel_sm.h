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

#if NEXUS_CHANNEL_ENABLED

#include "oc/include/oc_ri.h"
#include "oc/messaging/coap/coap.h"
#include "src/nexus_channel_core.h"
#include "src/nexus_channel_res_lm.h"

// struct for storing Nexus Channel-secured resource methods
typedef struct nexus_secured_resource_method_t
{
    struct nexus_secured_resource_method_t* next;
    oc_resource_t* resource;
    oc_method_t method;
} nexus_secured_resource_method_t;

// struct for storing COSE_MAC0 elements according to Nexus Channel Security
// Mode 0 specification
typedef struct nexus_security_mode0_cose_mac0_t
{
    uint8_t protected_header; // currently one byte
    uint32_t kid;
    uint32_t nonce;
    uint8_t* payload;
    uint8_t payload_len;
    struct nexus_check_value* mac;
} nexus_security_mode0_cose_mac0_t;

/** Initialize the Nexus Channel Security Manager module.
 *
 * Called on startup by `nexus_channel_core_init`.
 */
void nexus_channel_sm_init(void);

/** Register a Nexus Channel resource method and specify whether it
  * is secured. "Secured" means that only messages from another Nexus Channel
  * node with a currently valid Nexus Channel Link will be attempted to be
  * processed.
  *
  * Internally, this wraps `oc_resource_set_request_handler` and includes the
  * following changes:
  *
  * * removes unused `user_data` parameter
  * * prevents registration if a handler is already registered to this resource
  * for the requested method
  * * requires specification of Nexus Channel security
  *
  * \param resource pointer to the `oc_resource_t` to register the handler to
  * \param method method (`oc_method_t`) to register the handler to
  * \param callback pointer to the handler function to register
  * \return true if the registration was successful; false otherwise
  */
bool nexus_channel_sm_set_request_handler(oc_resource_t* resource,
                                          oc_method_t method,
                                          oc_request_callback_t callback,
                                          bool secured);

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

/** Compute MAC value from Nexus message contents as per Nexus Channel security
  * mode 0.
  *
  * MAC is computed over the nonce, payload, and CoAP header type code.
  *
  * The application can rely on the CoAP type code (CREATED, NOT AUTHORIZED,
  * etc) as well as the CBOR payload data to be authenticated. Other data in
  * the header is not secured.
  *
  * \param security_data Mode0 Security data associated with the message to
 * check
  * \param received_mac0 pointer to received mac0 struct (built from message to
 * check)
  * \return Nexus check value struct that represents the computed MAC given the
  * inputs
  */
struct nexus_check_value _nexus_channel_sm_compute_mac_mode0(
    const struct nexus_security_mode0_cose_mac0_t* const received_mac0,
    const struct nexus_channel_link_security_mode0_data* security_data);

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

/** Determines if the input oc_rep is in canonical Angaza-COSE_MAC0 format:
  *
  * 1. bytestring of any size (protected bucket), expected to be empty/0-length
  * 2. object/map of unprotected bucket, size 2
  *   2a. int (unprotected key ID)
  *   2b. int (unprotected nonce)
  * 3. bytestring of any size (payload)
  * 4. bytestring (MAC, expected to be 8 bytes)
  *
  * Due to limitations in the `oc_rep` module, we cannot make the COSE_MAC0
  * format as per its spec. Specifically, we cannot create an array with mixed
  * object/map and primitive types (like bytestrings).
  *
  * This format was chosen as a good compromise between understandability and
  * overhead per message. Angaza controls the format in the Nexus layer so
  * there is no risk that we will get a valid message with an unexpected
  * format.
  *
  * When we eventually switch to RFC-specified COSE_MAC0, then this method
  * should be updated -- all decoding of the security header is contained in
  * here.
  *
  * \param rep `oc_rep_t` representing the CBOR-encoded COSE_MAC0 to validate.
  * The rep shall not be modified, but the pointer can be modified (to traverse
  * the tree)
  * \param cose_mac0_parsed pointer to an object that contains all data stored
  * inside the COSE_MAC0 message. If parsing is successful then all data will
  * be stored in this struct for caller access
  * \return true if the input is in canonical Angaza-COSE_MAC0 format, false
  * otherwise
  */
bool _nexus_channel_sm_parse_cose_mac0(
    const oc_rep_t* rep, nexus_security_mode0_cose_mac0_t* cose_mac0_parsed);

/** Repack an authenticated CoAP message without its COSE_MAC0 header.
  *
  * This means we pack the parsed payload and update the application-content
  * type (since the packet no longer contains COSE_MAC0 information).
  *
  * \param pkt pointer to CoAP packet to repack
  * \param cose_mac0_parsed pointer to data parsed from COSE_MAC0 that will be
  * repacked into the CoAP packet (the payload and payload length)
  */
void _nexus_channel_sm_repack_no_cose_mac0(
    coap_packet_t* const pkt,
    const nexus_security_mode0_cose_mac0_t* const cose_mac0_parsed);
#endif

// Only defined for unit tests
#ifdef NEXUS_DEFINED_DURING_TESTING
uint8_t _nexus_channel_sm_secured_resource_methods_count(void);
#endif

#endif /* if NEXUS_CHANNEL_ENABLED */
#endif /* ifdef NEXUS__CHANNEL__SRC__NEXUS_CHANNEL_SM__H */
