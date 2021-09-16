/** \file nexus_cose_mac0.c
 * Nexus COSE MAC0 Encoding/Sign Module (Implementation)
 * \author Angaza
 * \copyright 2020-2021 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "src/nexus_cose_mac0_sign.h"
#include "oc/deps/tinycbor/src/cbor.h"

#if NEXUS_CHANNEL_LINK_SECURITY_ENABLED

    #define NEXUS_COSE_MAC0_VALID_COSE_MESSAGE_ARRAY_LENGTH 4

    #define NEXUS_COSE_MAC0_VALID_PROTECTED_HEADER_MAP_ELEMENT_COUNT 1

NEXUS_IMPL_STATIC nexus_cose_error
_nexus_cose_mac0_sign_input_and_tag_to_nexus_cose_mac0_message_t(
    const nexus_cose_mac0_common_macparams_t* const mac_params,
    const struct nexus_check_value* const tag,
    uint8_t* output,
    size_t output_size,
    size_t* encoded_bytes_count)
{
    CborEncoder enc, inner_enc, map_enc;
    CborError result;
    uint8_t tmp_buf[NEXUS_COSE_MAC0_MAX_PROTECTED_HEADER_BSTR_SIZE];

    // first, create the CBOR array of 4 elements (6.1)
    cbor_encoder_init(&enc, output, output_size, 0);

    result = cbor_encoder_create_array(&enc, &inner_enc, 4);
    if (result != CborNoError)
    {
        return NEXUS_COSE_ERROR_CBOR_ENCODER;
    }

    // Put protected header as a map in `tmp_buf`
    const uint8_t protected_header_len =
        nexus_cose_mac0_encode_protected_header_map(
            mac_params->nonce_to_protect,
            tmp_buf,
            NEXUS_COSE_MAC0_MAX_PROTECTED_HEADER_BSTR_SIZE);

    if (protected_header_len == 0)
    {
        // should always be able to wrap the nonce into a protected header
        return NEXUS_COSE_ERROR_CBOR_ENCODER;
    }

    result = cbor_encode_byte_string(&inner_enc, tmp_buf, protected_header_len);
    if (result != CborNoError)
    {
        return NEXUS_COSE_ERROR_CBOR_ENCODER;
    }

    // unprotected header (none for Nexus Channel Security Mode 0 -> empty map)
    result = cbor_encoder_create_map(&inner_enc, &map_enc, 0);
    if (result != CborNoError)
    {
        return NEXUS_COSE_ERROR_CBOR_ENCODER;
    }

    result = cbor_encoder_close_container(&inner_enc, &map_enc);
    if (result != CborNoError)
    {
        return NEXUS_COSE_ERROR_CBOR_ENCODER;
    }
    // payload
    result = cbor_encode_byte_string(
        &inner_enc, mac_params->payload, mac_params->payload_len);
    if (result != CborNoError)
    {
        return NEXUS_COSE_ERROR_CBOR_ENCODER;
    }

    // tag
    result =
        cbor_encode_byte_string(&inner_enc, tag->bytes, sizeof(tag->bytes));
    if (result != CborNoError)
    {
        return NEXUS_COSE_ERROR_CBOR_ENCODER;
    }
    result = cbor_encoder_close_container(&enc, &inner_enc);
    if (result != CborNoError)
    {
        return NEXUS_COSE_ERROR_CBOR_ENCODER;
    }

    *encoded_bytes_count = cbor_encoder_get_buffer_size(&enc, output);

    // even if the output buffer is sufficient in size to store the result,
    // return an error if the result would be larger than the configured
    // CBOR payload size
    if (*encoded_bytes_count > NEXUS_COSE_MAC0_MAX_ENCODED_CBOR_OBJECT_SIZE)
    {
        OC_WRN("Encoded bytes=%u, maximum permissible=%d",
               *encoded_bytes_count,
               NEXUS_COSE_MAC0_MAX_ENCODED_CBOR_OBJECT_SIZE);
        return NEXUS_COSE_ERROR_INPUT_DATA_INVALID;
    }

    return NEXUS_COSE_ERROR_NONE;
}

nexus_cose_error nexus_cose_mac0_sign_encode_message(
    const nexus_cose_mac0_common_macparams_t* const mac_params,
    uint8_t* output,
    size_t output_size,
    size_t* encoded_bytes_count)
{
    // temporary structures to hold message in processing
    // mac_struct buffer should be equal to the exact size we define this
    // structure as in `nexus_cose_mac0_common.h`
    struct nexus_cose_mac0_cbor_data_t mac_struct;
    NEXUS_STATIC_ASSERT(
        sizeof(((struct nexus_cose_mac0_cbor_data_t*) 0)->buf) ==
            4 + NEXUS_COSE_MAC0_MAX_PROTECTED_HEADER_BSTR_SIZE +
                NEXUS_COSE_MAC0_MAX_AAD_SIZE +
                NEXUS_COSE_MAC0_MAX_ENCODED_CBOR_OBJECT_SIZE,
        "`mac_struct` not expected size for secured messages");

    // output will simply be an unsecured payload
    if (output_size < NEXUS_COSE_MAC0_MAX_ENCODED_CBOR_OBJECT_SIZE)
    {
        return NEXUS_COSE_ERROR_BUFFER_TOO_SMALL;
    }

    nexus_cose_error result =
        nexus_cose_mac0_common_mac_params_to_mac_structure(mac_params,
                                                           &mac_struct);

    if (result != NEXUS_COSE_ERROR_NONE)
    {
        return result;
    }

    // Compute the tag (cannot fail)
    const struct nexus_check_value tag =
        nexus_cose_mac0_common_compute_tag(&mac_struct, mac_params->key);

    // Create the encoded COSE MAC0 output message and populate its size
    result = _nexus_cose_mac0_sign_input_and_tag_to_nexus_cose_mac0_message_t(
        mac_params, &tag, output, output_size, encoded_bytes_count);

    if (result != NEXUS_COSE_ERROR_NONE)
    {
        return result;
    }

    return NEXUS_COSE_ERROR_NONE;
}
#endif /* if NEXUS_CHANNEL_LINK_SECURITY_ENABLED */
