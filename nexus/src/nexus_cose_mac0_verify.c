/** \file nexus_cose_mac0_verify.c
 * Nexus COSE MAC0 Verify Module (Implementation)
 * \author Angaza
 * \copyright 2020-2021 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "src/nexus_cose_mac0_verify.h"
#include "oc/deps/tinycbor/src/cbor.h"

#if NEXUS_CHANNEL_LINK_SECURITY_ENABLED

    #define NEXUS_COSE_MAC0_VALID_COSE_MESSAGE_ARRAY_LENGTH 4

    #define NEXUS_COSE_MAC0_CBOR_SINGLE_BYTE_INT_LENGTH 24

NEXUS_IMPL_STATIC nexus_cose_error
_nexus_cose_mac0_verify_deserialize_protected_header(
    uint32_t* nonce,
    const uint8_t* const protected_header,
    size_t protected_header_len)
{
    // used for parsing map stored in protected header bstr
    CborParser prot_parser;
    CborValue prot_root, prot_map;

    size_t tmp_length = protected_header_len;
    // now, extract nonce from the map stored in `tmp_buffer`
    if (cbor_parser_init(
            protected_header, tmp_length, 0, &prot_parser, &prot_root) !=
        CborNoError)
    {
        return NEXUS_COSE_ERROR_CBOR_PARSER;
    }

    tmp_length = 0;
    if (!cbor_value_is_map(&prot_root) ||
        (cbor_value_get_map_length(&prot_root, &tmp_length) != CborNoError))
    {
        return NEXUS_COSE_ERROR_INPUT_DATA_INVALID;
    }
    if (tmp_length == 0)
    {
        return NEXUS_COSE_ERROR_INPUT_DATA_INVALID;
    }
    if (cbor_value_enter_container(&prot_root, &prot_map) != CborNoError)
    {
        return NEXUS_COSE_ERROR_CBOR_PARSER;
    }
    if (!cbor_value_is_integer(&prot_map))
    {
        OC_WRN("Expected map key to be integer, was not");
        return NEXUS_COSE_ERROR_INPUT_DATA_INVALID;
    }
    // advance to nonce value, ensure it is also an integer
    if (cbor_value_advance(&prot_map) != CborNoError ||
        (!cbor_value_is_integer(&prot_map)))
    {
        return NEXUS_COSE_ERROR_INPUT_DATA_INVALID;
    }
    uint64_t tmp_nonce;
    if (cbor_value_get_uint64(&prot_map, &tmp_nonce) != CborNoError)
    {
        return NEXUS_COSE_ERROR_INPUT_DATA_INVALID;
    }
    if (tmp_nonce > UINT32_MAX)
    {
        OC_WRN("Nonce value too large (doesn't fit in uint32...");
        return NEXUS_COSE_ERROR_INPUT_DATA_INVALID;
    }
    *nonce = (uint32_t) tmp_nonce;

    // advance to end, and attempt to exit map
    if (cbor_value_advance(&prot_map) ||
        (cbor_value_leave_container(&prot_root, &prot_map) != CborNoError))
    {
        return NEXUS_COSE_ERROR_CBOR_PARSER;
    }
    return NEXUS_COSE_ERROR_NONE;
}

nexus_cose_error nexus_cose_mac0_verify_deserialize_protected_message(
    const uint8_t* const data,
    size_t data_len,
    nexus_cose_mac0_extracted_cose_params_t* result)
{
    // used for parsing outer array
    CborParser parser;
    CborValue root, ary;

    // used when extracting protected header (map nested in bstr)
    uint8_t tmp_buffer[NEXUS_COSE_MAC0_MAX_PROTECTED_HEADER_BSTR_SIZE];

    if (cbor_parser_init(data, data_len, 0, &parser, &root) != CborNoError)
    {
        return NEXUS_COSE_ERROR_CBOR_PARSER;
    }

    size_t tmp_length = 0;
    if (!cbor_value_is_array(&root) ||
        cbor_value_get_array_length(&root, &tmp_length) != CborNoError)
    {
        return NEXUS_COSE_ERROR_CBOR_PARSER;
    }

    if (tmp_length != NEXUS_COSE_MAC0_VALID_COSE_MESSAGE_ARRAY_LENGTH)
    {
        return NEXUS_COSE_ERROR_INPUT_DATA_INVALID;
    }

    // we've confirmed there is an array of length 4. Enter it
    if (cbor_value_enter_container(&root, &ary) != CborNoError)
    {
        return NEXUS_COSE_ERROR_CBOR_PARSER;
    }
    // confirm first element is bytestring of nonzero length
    tmp_length = 0;
    if (!cbor_value_is_byte_string(&ary) ||
        cbor_value_get_string_length(&ary, &tmp_length) != CborNoError)
    {
        return NEXUS_COSE_ERROR_INPUT_DATA_INVALID;
    }
    if (tmp_length == 0)
    {
        return NEXUS_COSE_ERROR_INPUT_DATA_INVALID;
    }
    // Enter the protected header bstr and extract the map
    tmp_length = sizeof(tmp_buffer);
    if (cbor_value_copy_byte_string(&ary, tmp_buffer, &tmp_length, &ary) !=
        CborNoError)
    {
        // this error occurs only if the input data overflows the length
        // of `tmp_buffer`, so this indicates invalid input data (too long
        // protected header bytestring)
        return NEXUS_COSE_ERROR_INPUT_DATA_INVALID;
    }
    // Extract nonce from protected header. Function will also leave
    // the protected header and go back to outer array so that third parameter
    // map is next element to access.
    const nexus_cose_error prot_header_err =
        _nexus_cose_mac0_verify_deserialize_protected_header(
            &result->nonce, tmp_buffer, tmp_length);
    if (prot_header_err != NEXUS_COSE_ERROR_NONE)
    {
        return prot_header_err;
    }

    // expect second parameter (outer array) as map, and move to third element
    if (!cbor_value_is_map(&ary) || (cbor_value_advance(&ary) != CborNoError))
    {
        return NEXUS_COSE_ERROR_INPUT_DATA_INVALID;
    }

    // Expect third element to be payload, which is bytestring
    if (!cbor_value_is_byte_string(&ary))
    {
        return NEXUS_COSE_ERROR_INPUT_DATA_INVALID;
    }
    // `ary->ptr` should be pointing to a value indicating bytestring (we've
    // confirmed this above). Here, we assume *definite* bytestrings which
    // reduces size constraints of parsing. this is a safe assumption because
    // COSE specification indicates that CBOR used in COSE *must* have
    // definite lengths
    // (https://datatracker.ietf.org/doc/html/rfc8152#section-14) (if an
    // indefinite bytestring was used, 'result->payload' would point to
    // segmented CBOR string chunks, not a decoded string)
    if (cbor_value_calculate_string_length(&ary, &result->payload_len) !=
        CborNoError)
    {
        return NEXUS_COSE_ERROR_CBOR_PARSER;
    }
    // length fits in a single byte, skip 1 byte
    if (result->payload_len < NEXUS_COSE_MAC0_CBOR_SINGLE_BYTE_INT_LENGTH)
    {
        // if payload_len is 0, this will point to the tag identifier,
        // but we should ignore the payload if length is 0.
        result->payload = (uint8_t*) ary.ptr + 1;
    }
    else if (result->payload_len < UINT8_MAX)
    {
        result->payload = (uint8_t*) ary.ptr + 2;
    }
    // lengths longer than UINT8_MAX for payload are not supported, and we
    // expect application logic at a higher level to prevent payloads larger
    // than ~128 bytes anyhow
    else
    {
        return NEXUS_COSE_ERROR_INPUT_DATA_INVALID;
    }
    // advance to fourth element
    if (cbor_value_advance(&ary) != CborNoError)
    {
        return NEXUS_COSE_ERROR_CBOR_PARSER;
    }
    // Confirm fourth element (tag) is nonzero length bytestring
    tmp_length = 0;
    if (!cbor_value_is_byte_string(&ary) ||
        cbor_value_get_string_length(&ary, &tmp_length) != CborNoError)
    {
        return NEXUS_COSE_ERROR_INPUT_DATA_INVALID;
    }
    if (tmp_length == 0)
    {
        return NEXUS_COSE_ERROR_INPUT_DATA_INVALID;
    }
    // Copy tag and close array
    tmp_length = sizeof(result->tag.bytes);
    if (cbor_value_copy_byte_string(
            &ary, result->tag.bytes, &tmp_length, &ary) != CborNoError)
    {
        return NEXUS_COSE_ERROR_CBOR_PARSER;
    }
    if (cbor_value_leave_container(&root, &ary) != CborNoError)
    {
        return NEXUS_COSE_ERROR_CBOR_PARSER;
    }

    return NEXUS_COSE_ERROR_NONE;
}

nexus_cose_error nexus_cose_mac0_verify_message(
    const nexus_cose_mac0_verify_ctx_t* const verify_ctx,
    uint32_t* extracted_nonce,
    uint8_t** unsecured_payload,
    size_t* unsecured_payload_len)
{
    // extract protected header map, payload, and tag
    nexus_cose_mac0_extracted_cose_params_t extracted_params;
    nexus_cose_error result =
        nexus_cose_mac0_verify_deserialize_protected_message(
            verify_ctx->payload, verify_ctx->payload_len, &extracted_params);
    if (result != NEXUS_COSE_ERROR_NONE)
    {
        OC_WRN("Error deserializing COSE MAC0 protected message");
        return result;
    }

    // Repack as payload/context using input context key and AAD, but nonce and
    // payload from transmitted message
    nexus_cose_mac0_common_external_aad_t repacked_aad;
    memcpy((void*) &repacked_aad,
           &verify_ctx->aad,
           sizeof(nexus_cose_mac0_common_external_aad_t));
    OC_LOG("Verifying AAD. URI len %d. Nonce %d. Payload len %d). Key: ",
           repacked_aad.coap_uri_len,
           extracted_params.nonce,
           extracted_params.payload_len);
    OC_LOGbytes(verify_ctx->key->bytes, sizeof(struct nx_common_check_key));

    // used so we can reuse the same mac_params->mac_struct for both
    // verify and sign functionality
    nexus_cose_mac0_common_macparams_t repacked_mac_params = {
        verify_ctx->key,
        extracted_params.nonce,
        repacked_aad,
        extracted_params.payload,
        extracted_params.payload_len,
    };

    // convert repacked payload/context into MAC struct
    struct nexus_cose_mac0_cbor_data_t repacked_mac_struct;
    result = nexus_cose_mac0_common_mac_params_to_mac_structure(
        &repacked_mac_params, &repacked_mac_struct);
    if (result != NEXUS_COSE_ERROR_NONE)
    {
        OC_WRN("Error packing MAC parameters to MAC structure");
        return result;
    }

    // compute tag
    struct nexus_check_value computed_tag = nexus_cose_mac0_common_compute_tag(
        &repacked_mac_struct, repacked_mac_params.key);

    if (memcmp((const void*) &computed_tag,
               (const void*) &extracted_params.tag,
               sizeof(struct nexus_check_value)) != 0)
    {
        return NEXUS_COSE_ERROR_MAC_TAG_INVALID;
    }

    // caller will need to decide if the nonce is in range -- this function
    // only ensures that the nonce contained in the protected message is
    // unmodified (and was the same one used to generate the tag/MAC)
    *extracted_nonce = extracted_params.nonce;

    // set pointer value to point to first byte of encapsulated payload
    *unsecured_payload = extracted_params.payload;
    *unsecured_payload_len = extracted_params.payload_len;

    // Message deserialized and authenticated correctly
    return NEXUS_COSE_ERROR_NONE;
}

#endif /* if NEXUS_CHANNEL_LINK_SECURITY_ENABLED */
