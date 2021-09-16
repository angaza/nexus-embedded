/** \file
 * Nexus COSE MAC0 Common Functionality Module (Header)
 * \author Angaza
 * \copyright 2020-2021 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

// Types and functions used by `nexus_cose_mac0_sign.h` and
// `nexus_cose_mac0_verify.h`.
//
// This module is also aware of the "Nexus Channel" use of COSE MAC0 here,
// and assumes use in that context (Siphash 2-4 for auth/MAC, e.g.)

#ifndef __NEXUS__COMMON__SRC__COSE_MAC0_COMMON_H__
#define __NEXUS__COMMON__SRC__COSE_MAC0_COMMON_H__

#include "src/nexus_util.h"

#ifdef __cplusplus
extern "C" {
#endif

// Secured messages must still fit within the maximum CBOR payload size
// channel maximum CBOR size
#define NEXUS_COSE_MAC0_MAX_ENCODED_CBOR_OBJECT_SIZE                           \
    NEXUS_CHANNEL_MAX_CBOR_PAYLOAD_SIZE

// CoAP method code (1 byte)
// URI length field (2 bytes max)
// URI (11 bytes max)
#define NEXUS_COSE_MAC0_MAX_COAP_URI_LENGTH                                    \
    (NEXUS_CHANNEL_MAX_HUMAN_READABLE_URI_LENGTH)
#define NEXUS_COSE_MAC0_MAX_AAD_SIZE                                           \
    (1 + 2 + NEXUS_COSE_MAC0_MAX_COAP_URI_LENGTH)

// At most one element in the protected header (nonce)
#define NEXUS_COSE_MAC0_VALID_PROTECTED_HEADER_MAP_ELEMENT_COUNT 1

// 7 bytes for a protected header with uint32_t nonce
// A1             # map(1)
//   05          # unsigned(5)
//   1A FFFFFFFF # unsigned(4294967295)
//   Add one additional byte to provide for functions that treat the
//   protected header as a bytestring and
#define NEXUS_COSE_MAC0_MAX_PROTECTED_HEADER_BSTR_SIZE 7

/* Return codes specific to nexus_cose_mac0 functionality.
 *
 * Used for clearly diagnosing the cause of failure in encoding or decoding
 * COSE secured messages.
 */
typedef enum
{
    NEXUS_COSE_ERROR_NONE = 0,
    NEXUS_COSE_ERROR_BUFFER_TOO_SMALL,
    NEXUS_COSE_ERROR_CBOR_ENCODER,
    NEXUS_COSE_ERROR_CBOR_PARSER,
    NEXUS_COSE_ERROR_INPUT_DATA_INVALID,
    NEXUS_COSE_ERROR_MAC_TAG_INVALID,
} nexus_cose_error;

// Data carried outside of the payload itself, but also included in
// MAC0 computation; "AAD" stands for "additional authenticated data"
// https://tools.ietf.org/html/rfc8152#section-4.3
typedef struct nexus_cose_mac0_common_external_aad_t
{
    uint8_t coap_method;

    // 'my/coap/uri'
    uint8_t* coap_uri;
    uint8_t coap_uri_len;
} nexus_cose_mac0_common_external_aad_t;

/* Parameters used when generating a COSE Mac0 structure
 * (`nexus_cose_mac0_cbor_data_t`)
 */
typedef struct nexus_cose_mac0_common_macparams_t
{
    // key used to compute MAC/tag
    const struct nx_common_check_key* key;
    // will be placed in protected header
    uint32_t nonce_to_protect;
    // CoAP method, URI, URI length
    nexus_cose_mac0_common_external_aad_t aad;

    const uint8_t* payload;
    size_t payload_len;
} nexus_cose_mac0_common_macparams_t;

/* Section 6.3, "MAC_structure". Fields used when computing a MAC/tag.
 *
 * Represents a valid CBOR array with 4 elements as described by RFC 8152
 * section 6.3.
 */
typedef struct nexus_cose_mac0_cbor_data_t
{
    // MAC0 structure holds the context string ("MAC0"), protected header, AAD,
    // and payload
    uint8_t buf[4 + NEXUS_COSE_MAC0_MAX_PROTECTED_HEADER_BSTR_SIZE +
                NEXUS_COSE_MAC0_MAX_AAD_SIZE +
                NEXUS_COSE_MAC0_MAX_ENCODED_CBOR_OBJECT_SIZE];
    uint8_t len;
} nexus_cose_mac0_cbor_data_t;

/* Given a key and mac struct, compute the resulting MAC0 MAC/tag value.
 *
 * Computes a MAC per section 6.3. `mac_struct` is assumed to already be
 * a valid CBOR bytestream.
 *
 * Note: Only one algorithm is currently used for the MAC - Siphash 2-4
 * MAC computation - so it is not specified in signing or verification.
 *
 * \param mac_struct mac_struct (as valid CBOR) to compute tag/MAC over
 * \param key key to use to compute tag/MAC
 * \return computed nexus_check_value representing tag
 */
struct nexus_check_value nexus_cose_mac0_common_compute_tag(
    const struct nexus_cose_mac0_cbor_data_t* const mac_struct,
    const struct nx_common_check_key* const key);

/* Extract data from `input` into a "MAC_Structure" for further processing.
 *
 * Typically, the "MAC_Structure" is then passed to
 * `nexus_cose_mac0_common_compute_tag` to generate a MAC using a given key.
 *
 * CDDL for the MAC structure is below:
 *
 *   MAC_structure = [
 *   context : "MAC0",  // fixed value
 *   protected : empty_or_serialized_map,
 *   external_aad : bstr,
 *   payload : bstr
 *   ]
 *
 * Assumes Nexus Channel Security Mode 0 (uses nonce, symmetric keying, etc).
 *
 * Extracts nonce into protected data bucket (little-endian ordered bstr) under
 * header parameter 'IV', label value '5'. (Spec Table 2).
 *
 * Packs `coap_method` as first byte of AAD, and remaining bytes of AAD are
 * the `coap_uri`.
 *
 * Payload is the payload provided by `mac_params`.
 *
 * \param mac_params context and payload to fill into a mac_struct
 * \param mac_struct buffer to store resulting CBOR-encoded MAC struct
 * \return error if unable to populate `mac_struct` based on `input`
 */
nexus_cose_error nexus_cose_mac0_common_mac_params_to_mac_structure(
    const nexus_cose_mac0_common_macparams_t* const mac_params,
    struct nexus_cose_mac0_cbor_data_t* mac_struct);

/* Given a nonce, generate a CBOR map representing the protected header.
 *
 * Does not use `nexus_cose_mac0_cbor_data_t` to save RAM (don't need
 * as much buffer space).
 *
 * Returns 0 if unable to copy nonce into protected header buffer.
 *
 * \param nonce nonce to place into protected header
 * \param protected_header_buf buffer to store protected header bytes into
 * \param protected_header_size size of buffer
 * \return number of bytes copied into `protected_header_buf`
 */
uint8_t
nexus_cose_mac0_encode_protected_header_map(uint32_t nonce,
                                            uint8_t* protected_header_buf,
                                            uint8_t protected_header_size);
#ifdef __cplusplus
}
#endif

#endif /* __NEXUS__COMMON__SRC__COSE_MAC0_COMMON_H_ */
