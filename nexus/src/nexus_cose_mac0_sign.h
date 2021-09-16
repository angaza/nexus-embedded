/** \file
 * Nexus COSE MAC0 Encoding/Sign Module (Header)
 * \author Angaza
 * \copyright 2020-2021 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

// COSE MAC0 implementation based on RFC 8152
// Used to generate and authenticate untagged COSE MAC0 structs
// MAC0 = COSE MAC w/o recipients object
//
// "Untagged" means that the COSE MAC0 struct will be encoded with CBOR
// without a semantic tag; consumers of this struct will know that
// it is COSE MAC0 from other application layer context. For example,
// Angaza Nexus Channel will use the CoAP content-format option which indicates
// the payload is COSE_MAC0.
//
// Algorithm is implicit and fixed as Siphash 2-4 (using Nexus Channel
// shared link key as key, and COSE MAC struct as input data) to generate
// a new MAC/tag. Note that Siphash 2-4 is not an official RFC-supported
// algorithm.
// https://tools.ietf.org/html/rfc8152

#ifndef __NEXUS__COMMON__SRC__COSE_MAC0_SIGN_H_
#define __NEXUS__COMMON__SRC__COSE_MAC0_SIGN_H_

#include "src/nexus_cose_mac0_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Given security context and unsecured payload, create a secured COSE MAC0
 * message (Nexus Channel Link Security Mode 0).
 *
 * All fields of `mac_params` must be initialized before passing it to
 * this function.
 *
 * If this function is successful, the information from `mac_params` (including
 * payload) will be used to encode the *unsecured* CBOR message as a COSE MAC0
 * CBOR payload stored in `output`, with size `encoded_bytes_count`.
 *
 * If this function returns any value other than `NEXUS_COSE_ERROR_NONE`,
 * the message was not successfully encoded, and the `output` buffer must
 * be disregarded.
 *
 * \param mac_params parameters used to create a secured payload
 * \param output buffer to write secured output payload
 * \param output_size max number of bytes to write to `output`.
 * \param encoded_bytes_count actual number of bytes written to `output`
 * \return error if failed, otherwise no error
 */
nexus_cose_error nexus_cose_mac0_sign_encode_message(
    const nexus_cose_mac0_common_macparams_t* const mac_params,
    uint8_t* output,
    size_t output_size,
    size_t* encoded_bytes_count);

// Functions only exposed externally during unit testing
#ifdef NEXUS_INTERNAL_IMPL_NON_STATIC

/* Given an input payload, context, and computed tag/MAC, generate MAC0 Message.
 *
 * Will construct the secured message, which will contain:
 *
 * * Protected parameters = nonce
 * * No unprotected parameters
 * * Payload
 * * Tag
 *
 * \param mac_params payload and parameters will be extracted from this parameter
 * \param tag computed MAC/tag for the secured MAC0 message
 * \param output pointer to location to write secured message into
 * \param output_size max number of bytes to write into `output`.
 * \param encoded_bytes_count number of bytes written into `output`
 * \return error if unable to populate `secure_message`, no error otherwise
 */
nexus_cose_error
_nexus_cose_mac0_sign_input_and_tag_to_nexus_cose_mac0_message_t(
    const nexus_cose_mac0_common_macparams_t* const mac_params,
    const struct nexus_check_value* const tag,
    uint8_t* output,
    size_t output_size,
    size_t* encoded_bytes_count);
#endif /* #ifdef NEXUS_INTERNAL_IMPL_NON_STATIC */

#ifdef __cplusplus
}
#endif

#endif /* __NEXUS__COMMON__SRC__COSE_MAC0_SIGN_H_ */
