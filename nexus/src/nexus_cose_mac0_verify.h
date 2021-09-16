/** \file
 * Nexus COSE MAC0 Verify Module (Header)
 * \author Angaza
 * \copyright 2020-2021 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

// Verify messages which were signed by functions provided by
// `nexus_cose_mac0_sign.h`.
//
// This API provides the necessary functions to take an incoming CBOR
// payload which contains a COSE MAC0 structure.
//
// Given a key, it is possible to verify if the provided structure is valid
// (was signed with the same symmetric key/tag generated using that key).
// See also: `nexus_cose_mac0_sign.h`

#ifndef __NEXUS__COMMON__SRC__COSE_MAC0_VERIFY_H__
#define __NEXUS__COMMON__SRC__COSE_MAC0_VERIFY_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "src/nexus_cose_mac0_common.h"

/* Information required to verify an incoming MAC0 payload.
 */
typedef struct nexus_cose_mac0_verify_ctx_t
{
    const struct nx_common_check_key* key;
    // CoAP method, URI, URI length
    nexus_cose_mac0_common_external_aad_t aad;

    uint8_t* payload;
    size_t payload_len;
} nexus_cose_mac0_verify_ctx_t;

typedef struct nexus_cose_mac0_extracted_cose_params_t
{
    uint32_t nonce;
    uint8_t* payload;
    size_t payload_len;
    struct nexus_check_value tag;
} nexus_cose_mac0_extracted_cose_params_t;

/* Given a verification context and secured payload, verify payload.
 * (Nexus Channel Link Security Mode 0).
 *
 * A message successfully created via `nexus_cose_mac0_sign_encode_message`
 * should always return true when examined with this function and the same
 * security context.
 *
 * In other words, copying the `output` from `nexus_cose_mac0_encode_message`
 * to a new `input` struct with the same security context, and passing that
 * `input` struct to this function should always return 'NEXUS_COSE_ERROR_NONE'.
 *
 * If this function returns true, `extracted_nonce` will be populated with
 * the nonce from the input message, `unsecured_payload` will be updated to
 * point to the first byte of the contained payload from the input message,
 * and `unsecured_payload_len` will be populated with the length of the payload.
 *
 * If this function returns false, `extracted_nonce` and `unsecured_payload`
 * are not modified.
 *
 * This function does not copy or extract `unsecured_payload`, the caller
 * may copy `unsecured_payload_len` bytes from `unsecured_payload` if
 * necessary.
 *
 * \warning assumes that `unsecured_payload` points to a buffer at least as
 * long as the `verify_ctx->payload_len`.
 *
 * \param verify_ctx input payload and context to examine
 * \param extracted_nonce nonce from received message
 * \param unsecured_payload pointer which will be updated to point to encapsulated unsecured payload
 * \param unsecured_payload_len length of payload beginning at `unsecured_payload`
 * \return error if unable to verify message, no error otherwise
 */
nexus_cose_error nexus_cose_mac0_verify_message(
    const nexus_cose_mac0_verify_ctx_t* const verify_ctx,
    uint32_t* extracted_nonce,
    uint8_t** unsecured_payload,
    size_t* unsecured_payload_len);

#ifdef NEXUS_INTERNAL_IMPL_NON_STATIC
nexus_cose_error _nexus_cose_mac0_verify_deserialize_protected_header(
    uint32_t* nonce,
    const uint8_t* const protected_header,
    size_t protected_header_len);
#endif // NEXUS_INTERNAL_IMPL_NON_STATIC

/* Given a CBOR struct, extract it into an internal representation for
 * further processing.
 *
 * Does *not* perform authentication with nonce/key, is just concerned with
 * formatting. Checks:
 * - Message is a CBOR array with 4 elements
 * - Protected header is not empty, and is bstr (1st element)
 * - Unprotected header is map (2nd element)
 * - Payload *may* be empty, but is bstr (3rd element)
 * - Tag is not empty, and is bstr (4th element)
 *
 *   If message is valid, data is extracted and placed into `result`.
 *   If message is invalid, `result` may be partially populated. The
 *   value of `result` is only valid if this function returns
 * `NEXUS_COSE_ERROR_NONE`.
 *
 * \param data pointer to data to check for COSE MAC0 secured message
 * \param data_len number of bytes to examine in data
 * \param result protected header, payload, and tag to populate
 * \return error if unable to deserialize message per the rules above
 */
nexus_cose_error nexus_cose_mac0_verify_deserialize_protected_message(
    const uint8_t* const data,
    size_t data_len,
    struct nexus_cose_mac0_extracted_cose_params_t* result);

#ifdef __cplusplus
}
#endif

#endif /* __NEXUS__COMMON__SRC__COSE_MAC0_COMMON_H_ */
