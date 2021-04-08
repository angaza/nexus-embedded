/** \file nexus_oc_wrapper.h
 * Nexus-OC Wrapper Module (Header)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#ifndef NEXUS__SRC__NEXUS_OC_WRAPPER_INTERNAL_H_
#define NEXUS__SRC__NEXUS_OC_WRAPPER_INTERNAL_H_

#include "src/nexus_channel_core.h"

#if NEXUS_CHANNEL_CORE_ENABLED

    #include <stdbool.h>
    #include <stdint.h>

    #include "oc/include/oc_ri.h"
    #include "oc/messaging/coap/transactions.h"
    #include "src/nexus_channel_sm.h"

    #ifdef __cplusplus
extern "C" {
    #endif

// "All OCF Nodes" FF0X:0:0:0:0:0:0:158
extern oc_endpoint_t NEXUS_OC_WRAPPER_MULTICAST_OC_ENDPOINT_T_ADDR;
// Equivalent Nexus Channel ID
extern struct nx_id NEXUS_OC_WRAPPER_MULTICAST_NX_ID;

/** Wrapper for `oc_add_resource` method.
 *
 * Thin wrapper for Nexus includes the following changes:
 *
 * * don't allow registration to a URI that's already been registered
 * Used internally during initial resource registration.
 *
 * \param resource the resource to add to the Nexus application
 * \return true if the resource could be added
 */
bool nexus_add_resource(oc_resource_t* resource);

/** Register a Nexus Channel resource method and specify whether it
 * is secured. "Secured" means that only messages from another Nexus Channel
 * node with a currently valid Nexus Channel Link will be attempted to be
 * processed.
 *
 * Internally, this wraps `oc_resource_set_request_handler` and includes the
 * following changes:
 *
 * * removes unused `user_data` parameter
 * * prevents registration if a handler is already registered to this
 * resource for the requested method
 * * if `secured`, will attempt to secure resource method using Nexus Channel
 *
 * \param resource pointer to the `oc_resource_t` to register the handler to
 * \param method method (`oc_method_t`) to register the handler to
 * \param callback pointer to the handler function to register
 * \param secured true if method should be secured by Nexus Channel
 * \return NX_CHANNEL_ERROR_NONE if request handler set successfully
 */
nx_channel_error
nexus_channel_set_request_handler(oc_resource_t* resource,
                                  oc_method_t method,
                                  oc_request_callback_t callback,
                                  bool secured);

/** Convert a Nexus ID into an IPV6 address.
 *
 * All Nexus IDs may be represented as valid link-local or globally valid
 * addresses depending on whether the Nexus ARIN global prefix is used. Link
 * local is sufficient for most use cases.
 *
 * This function returns a link-local address through the `dest_address`
 * pointer.
 *
 * The steps taken to convert a Nexus ID into an IPV6 address:
 *
 * 1) Represent `authority_id` as big-endian (network order).
 * 2) Represent `device_id` as big-endian (network order).
 * 3) Concatenate the leftmost/'first' byte of `device_id` to `authority_id`
 * 4) Treat this three byte block as an OUI and flip the 7th bit from the
 * left 5) Concatenate bytes [0xFF, 0xFE] to the three bytes already
 * concatenated 6) Concatenate the remaining three bytes of `device_id` to
 * the five bytes already concatenated 7) Prepend [0xFE, 0x80, 0x00, 0x00,
 * 0x00, 0x00, 0x00, 0x00] (0xFE80 is 'link local' scope) 8) The resulting
 * 16-byte string is a valid link-local IPV6 address
 *
 * \param id nx_id to convert to IPV6 address
 * \param dest_address pointer to IPV6 address struct to populate
 */
void nexus_oc_wrapper_nx_id_to_oc_endpoint(const struct nx_id* id,
                                           struct oc_endpoint_t* dest_address);

/** Convenience to convert an OC endpoint IPV6 address to Nexus IPV6.
 *
 * \param source_endpoint oc_endpoint_t to convert
 * \param dest_nx_id output for nx_ipv6_address format
 */
void nexus_oc_wrapper_oc_endpoint_to_nx_id(
    const oc_endpoint_t* const source_endpoint, struct nx_id* dest_nx_id);

    #if NEXUS_CHANNEL_LINK_SECURITY_ENABLED
/** Repack a CBOR-encoded payload with Nexus Channel security.
 *
 * \param buffer pointer to buffer to repack with Nexus Channel security
 * \param buffer_size size of buffer to repack
 * \param cose_mac0 pointer to COSE_MAC0 data to use in creating the
 * security primitives
 * \return new size of the COSE_MAC0 packed buffer
 */
uint8_t nexus_oc_wrapper_repack_buffer_secured(
    uint8_t* buffer,
    uint8_t buffer_size,
    nexus_security_mode0_cose_mac0_t* cose_mac0);

/** Repack a CBOR-encoded payload *without* Nexus Channel security.
 *
 * Takes a payload that has been previously secured via
 * `nexus_oc_wrapper_repack_buffer_secured` and unwraps it, extracting
 * the embedded payload only. No validation of security is performed
 * by this function.
 *
 * Makes a local copy of contents in `buffer`, and will copy the
 * 'unsecured' payload back into the original `payload_buffer`,
 * destructively overwriting the original secured content.
 *
 * If this function returns false, `payload_buffer` and
 * `unsecured_payload_size` are unmodified.
 *
 * \param payload_buffer pointer to COSE_MAC0 payload to extract from
 * \param secured_payload_size number of secured message bytes in `payload_buffer`
 * \param unsecured_payload_size Final 'unsecured' message size
 * \return true if unpacked successfully, false otherwise
 */
bool nexus_oc_wrapper_extract_embedded_payload_from_mac0_payload(
    uint8_t* payload_buffer,
    uint8_t secured_payload_size,
    uint8_t* unsecured_payload_size);

    #endif /* NEXUS_CHANNEL_LINK_SECURITY_ENABLED */

    #ifdef __cplusplus
}
    #endif

#endif /* NEXUS_CHANNEL_CORE_ENABLED */
#endif /* ifndef NEXUS__SRC__NEXUS_OC_WRAPPER_INTERNAL_H_ */
