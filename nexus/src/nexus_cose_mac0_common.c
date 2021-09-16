/** \file nexus_cose_mac0_common.c
 * Nexus COSE MAC0 Common Functionality Module (Implementation)
 * \author Angaza
 * \copyright 2020-2021 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "src/nexus_cose_mac0_common.h"
#include "oc/deps/tinycbor/src/cbor.h"
#include "src/nexus_util.h"

#if NEXUS_CHANNEL_LINK_SECURITY_ENABLED

    // Table 2 from RFC 8152 (5 = IV)
    #define NEXUS_COSE_MAC0_NONCE_IV_LABEL_KEY 5

    #define NEXUS_COSE_MAC0_VALID_PROTECTED_HEADER_MAP_ELEMENT_COUNT 1

    #define NEXUS_COSE_MAC0_SECTION_6_3_CONTEXT_STRING "MAC0"
    #define NEXUS_COSE_MAC0_SECTION_2_COSE_MESSAGE_ARRAY_LENGTH 4

    // Conservative, anticipate most URIs < 10 characters
    #define NEXUS_COSE_MAC0_MAX_URI_LENGTH 25
    // +1 for the single byte required to store the CoAP request/response code
    #define NEXUS_COSE_MAC0_MAX_AAD_SIZE_FOR_CREATING_MAC_STRUCT               \
        (NEXUS_COSE_MAC0_MAX_URI_LENGTH + 1)

uint8_t
nexus_cose_mac0_encode_protected_header_map(uint32_t nonce,
                                            uint8_t* protected_header_buf,
                                            uint8_t protected_header_buf_size)
{
    CborEncoder enc, inner_enc;
    uint8_t hdr_buf[NEXUS_COSE_MAC0_MAX_PROTECTED_HEADER_BSTR_SIZE];

    if (protected_header_buf_size <
        NEXUS_COSE_MAC0_MAX_PROTECTED_HEADER_BSTR_SIZE)
    {
        return 0;
    }

    const uint32_t nonce_le = nexus_endian_htole32(nonce);
    cbor_encoder_init(&enc, hdr_buf, sizeof(hdr_buf), 0);

    if (cbor_encoder_create_map(&enc, &inner_enc, 1) != CborNoError)
    {
        return 0;
    }
    if (cbor_encode_uint(&inner_enc, NEXUS_COSE_MAC0_NONCE_IV_LABEL_KEY) !=
        CborNoError)
    {
        return 0;
    }
    if (cbor_encode_uint(&inner_enc, nonce_le) != CborNoError)
    {
        return 0;
    }
    if (cbor_encoder_close_container(&enc, &inner_enc) != CborNoError)
    {
        return 0;
    }

    const size_t header_size = cbor_encoder_get_buffer_size(&enc, hdr_buf);
    NEXUS_ASSERT(header_size <= NEXUS_COSE_MAC0_MAX_PROTECTED_HEADER_BSTR_SIZE,
                 "nexus_cose_mac0: Encoded protected header map too large");
    memcpy(protected_header_buf, hdr_buf, header_size);
    return (uint8_t) header_size;
}

nexus_cose_error nexus_cose_mac0_common_mac_params_to_mac_structure(
    const nexus_cose_mac0_common_macparams_t* const mac_params,
    struct nexus_cose_mac0_cbor_data_t* mac_struct)
{
    CborEncoder enc, inner_enc;

    // used internally to build components of the MAC struct array. Also
    // used to temporarily store the protected header for a different
    // operation
    NEXUS_STATIC_ASSERT(NEXUS_COSE_MAC0_MAX_AAD_SIZE_FOR_CREATING_MAC_STRUCT >
                            7,
                        "tmp_buf too small to store protected header");
    uint8_t tmp_buf[NEXUS_COSE_MAC0_MAX_AAD_SIZE_FOR_CREATING_MAC_STRUCT];

    // Put protected header as a map in `tmp_buf`
    const uint8_t protected_header_len =
        nexus_cose_mac0_encode_protected_header_map(
            mac_params->nonce_to_protect, tmp_buf, sizeof(tmp_buf));
    if (protected_header_len == 0)
    {
        // should never occur, but don't attempt to continue if it does
        return NEXUS_COSE_ERROR_CBOR_ENCODER;
    }

    // Initialize CBOR encoder to encode a `MAC_structure` (Section 6.3)
    cbor_encoder_init(&enc, mac_struct->buf, sizeof(mac_struct->buf), 0);

    // 4 item array (identity string 'MAC0', protected attributes, AAD, payload)
    if (cbor_encoder_create_array(
            &enc,
            &inner_enc,
            NEXUS_COSE_MAC0_SECTION_2_COSE_MESSAGE_ARRAY_LENGTH) != CborNoError)
    {
        NEXUS_ASSERT(false, "Unexpectedly unable to initialize array_encoder");
        return NEXUS_COSE_ERROR_CBOR_ENCODER;
    }

    // 6.3.1 - "MAC0" is 4 characters long
    NEXUS_ASSERT(strlen(NEXUS_COSE_MAC0_SECTION_6_3_CONTEXT_STRING) == 4,
                 "Invalid MAC0 context string");

    if (cbor_encode_text_string(&inner_enc,
                                NEXUS_COSE_MAC0_SECTION_6_3_CONTEXT_STRING,
                                4) != CborNoError)
    {
        return NEXUS_COSE_ERROR_CBOR_ENCODER;
    }

    // 6.3.2 protected header as bytestring (0 length bytestring if empty)
    if (cbor_encode_byte_string(&inner_enc, tmp_buf, protected_header_len) !=
        CborNoError)
    {
        return NEXUS_COSE_ERROR_CBOR_ENCODER;
    }

    // 6.3.3 AAD encoded as bytestring.
    // First, copy the CoAP method, then the URI
    tmp_buf[0] = mac_params->aad.coap_method;
    // catch too-long URIs
    if (mac_params->aad.coap_uri_len > (NEXUS_COSE_MAC0_MAX_COAP_URI_LENGTH))
    {
        OC_WRN("CoAP URI too long, cannot build MAC struct");
        return NEXUS_COSE_ERROR_INPUT_DATA_INVALID;
    }
    // Skip 0-length URIs
    if (mac_params->aad.coap_uri_len > 0)
    {
        memcpy(&tmp_buf[1],
               mac_params->aad.coap_uri,
               mac_params->aad.coap_uri_len);
    }
    // "+1" here to include the CoAP method byte as well
    if (cbor_encode_byte_string(
            &inner_enc, tmp_buf, 1 + mac_params->aad.coap_uri_len) !=
        CborNoError)
    {
        return NEXUS_COSE_ERROR_CBOR_ENCODER;
    }

    // 6.3.4 payload
    // XXX we might optimize here by not copying payload at this stage,
    // computing the partial MAC without the payload, then computing the
    // MAC over the payload without copying, and inserting it into the tag
    // at the last step. We could do this by passing payload to the tag
    // generation step, and reduce the size of
    // `nexus_cose_mac0_cbor_data_t->buf`
    if (cbor_encode_byte_string(&inner_enc,
                                mac_params->payload,
                                mac_params->payload_len) != CborNoError)
    {
        return NEXUS_COSE_ERROR_CBOR_ENCODER;
    }

    // close array (checks that 4 elements are present, and that there
    // is sufficient memory to close it).
    if (cbor_encoder_close_container(&enc, &inner_enc) != CborNoError)
    {
        return NEXUS_COSE_ERROR_CBOR_ENCODER;
    }
    mac_struct->len =
        (uint8_t) cbor_encoder_get_buffer_size(&enc, mac_struct->buf);

    return NEXUS_COSE_ERROR_NONE;
}

struct nexus_check_value nexus_cose_mac0_common_compute_tag(
    const struct nexus_cose_mac0_cbor_data_t* const mac_struct,
    const struct nx_common_check_key* const key)
{
    const struct nexus_check_value computed_mac =
        nexus_check_compute(key, mac_struct->buf, mac_struct->len);
    return computed_mac;
}

#endif /* if NEXUS_CHANNEL_LINK_SECURITY_ENABLED */
