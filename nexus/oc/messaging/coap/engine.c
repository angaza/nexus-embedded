/*
// Copyright (c) 2016 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/
/*
 *
 * Copyright (c) 2013, Institute for Pervasive Computing, ETH Zurich
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 * Modifications (c) 2020 Angaza, Inc.
 */

#pragma GCC diagnostic push
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#pragma GCC diagnostic ignored "-Wcomment"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

// engine.h includes oc_config which provides OC_CLIENT/OC_SERVER
#include "engine.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api/oc_events.h"
#include "include/nxp_common.h"
#include "oc_api.h"
#include "oc_buffer.h"
#include "src/nexus_channel_sm.h"
#include "src/nexus_cose_mac0_verify.h"
#include "src/nexus_oc_wrapper.h"
#include "src/nexus_security.h"

/*
#ifdef OC_SECURITY
#include "security/oc_tls.h"
#endif // OC_SECURITY

#ifdef OC_BLOCK_WISE
#include "oc_blockwise.h"
#endif // OC_BLOCK_WISE
*/

#ifdef OC_CLIENT
    #include "oc_client_state.h"
#endif // OC_CLIENT
OC_PROCESS(coap_engine, "CoAP Engine");
/*
#ifdef OC_BLOCK_WISE
extern bool oc_ri_invoke_coap_entity_handler(
  void *request, void *response, oc_blockwise_state_t **request_state,
  oc_blockwise_state_t **response_state, uint16_t block2_size,
  oc_endpoint_t *endpoint);
#else  /* OC_BLOCK_WISE
*/
extern bool oc_ri_invoke_coap_entity_handler(void* request,
                                             void* response,
                                             uint8_t* buffer,
                                             oc_endpoint_t* endpoint);
//#endif // !OC_BLOCK_WISE

#if NEXUS_CHANNEL_OC_ENABLE_DUPLICATE_MESSAGE_ID_CHECK
    #define OC_REQUEST_HISTORY_SIZE (250)
static uint16_t history[OC_REQUEST_HISTORY_SIZE];
static uint8_t history_dev[OC_REQUEST_HISTORY_SIZE];
static uint8_t idx;

static bool check_if_duplicate(uint16_t mid, uint8_t device)
{
    size_t i;
    for (i = 0; i < OC_REQUEST_HISTORY_SIZE; i++)
    {
        if (history[i] == mid && history_dev[i] == device)
        {
            OC_DBG("dropping duplicate request");
            return true;
        }
    }
    return false;
}
#endif // #if NEXUS_CHANNEL_OC_ENABLE_DUPLICATE_MESSAGE_ID_CHECK

#if NEXUS_CHANNEL_LINK_SECURITY_ENABLED
static void
coap_send_previous_request_with_repacked_security(uint16_t mid,
                                                  const oc_endpoint_t* endpoint)
{
    coap_transaction_t* request_transaction = coap_get_transaction_by_mid(mid);

    if (request_transaction == NULL)
    {
        OC_WRN("No previous request transaction found");
        // no op - no previous request to send
        return;
    }

    // no need to allocate another oc_message, we will reuse the one that
    // is preserved in the buffered transaction.
    //
    oc_message_t* request_message = request_transaction->message;
    if (request_message == NULL)
    {
        OC_WRN("Found transaction to resend, but no message");
        // ensure there *was* some message for the transaction (should be)
        coap_clear_transaction(request_transaction);
        return;
    }
    else if (oc_endpoint_compare(&request_message->endpoint, endpoint) != 0)
    {
        OC_WRN(
            "Found transaction matching message ID, but not matching endpoint");
        return;
    }
    NEXUS_ASSERT(request_message->length > 0, "Message unexpectedly 0 length");

    OC_DBG("Repacking security for transaction with MID=%d",
           (int) request_transaction->mid);
    coap_packet_t pkt[1];
    // will be populated with repacked COSE MAC0 data
    uint8_t coap_payload_buffer[NEXUS_CHANNEL_MAX_CBOR_PAYLOAD_SIZE];

    const coap_status_t parse_result = coap_udp_parse_message(
        pkt, request_message->data, request_message->length);
    if (parse_result != COAP_NO_ERROR)
    {
        OC_WRN("Unable to parse CoAP packet from stored transaction...");
        coap_clear_transaction(request_transaction);
        return;
    }
    // here, we don't want to verify the message MAC, we just want to extract
    // the unsecured payload from the previously sent request.
    nexus_cose_mac0_extracted_cose_params_t extracted_params;
    const nexus_cose_error cose_result =
        nexus_cose_mac0_verify_deserialize_protected_message(
            pkt->payload, pkt->payload_len, &extracted_params);
    if (cose_result != NEXUS_COSE_ERROR_NONE)
    {
        OC_WRN("Error deserializing COSE MAC0 protected message (transaction)");
        coap_clear_transaction(request_transaction);
        return;
    }
    // here, we find the payload of the original message, and point to it.
    // we will then resecure and regenerate the message - storing it in a
    // local buffer in this function, then recopying it back over the
    // transaction message buffer (and updating length).
    struct nx_id nexus_id = {0};
    (void) nexus_oc_wrapper_oc_endpoint_to_nx_id(&request_message->endpoint,
                                                 &nexus_id);
    struct nexus_channel_link_security_mode0_data sec_data = {0};
    bool sec_data_exists = nexus_channel_link_manager_security_data_from_nxid(
        &nexus_id, &sec_data);
    if (!sec_data_exists)
    {
        OC_WRN("Secured message is to an endpoint we have no link to...");
        coap_clear_transaction(request_transaction);
        return;
    }

    // repack the original payload with an updated nonce
    nexus_cose_mac0_common_macparams_t mac_params = {
        &sec_data.sym_key,
        // nonce would have been updated before this re-request, but
        // we need to increment again because the server has already
        // updated its nonce to the same value and it expects to
        // receive a request with a nonce greater than its own
        sec_data.nonce + 1,
        // aad
        {
            pkt->code,
            (uint8_t*) pkt->uri_path,
            (uint8_t) pkt->uri_path_len,
        },
        extracted_params.payload,
        extracted_params.payload_len,
    };

    pkt->payload_len = nexus_oc_wrapper_repack_buffer_secured(
        coap_payload_buffer, sizeof(coap_payload_buffer), &mac_params);
    pkt->payload = coap_payload_buffer;

    // set the header content-format to indicate the payload is
    // secured
    coap_set_header_content_format(pkt, APPLICATION_COSE_MAC0);

    // securely clear the Nexus Channel security data from the
    // stack
    nexus_secure_memclr(&sec_data,
                        sizeof(struct nexus_channel_link_security_mode0_data),
                        sizeof(struct nexus_channel_link_security_mode0_data));

    // transfer from coap packet to message->data
    size_t len = coap_serialize_message(pkt, request_message->data);
    if (len > 0)
    {
        request_message->length = len;
        oc_send_message(request_message);
    }
    if (request_message->ref_count == 0)
    {
        oc_message_unref(request_message);
    }
}

// always a fixed code of 4.06
static void coap_send_nonce_sync_response(uint16_t mid,
                                          const uint8_t* token,
                                          size_t token_len,
                                          const oc_endpoint_t* endpoint,
                                          bool is_nonce_reset)
{
    coap_packet_t pkt[1];
    uint8_t coap_payload_buffer[NEXUS_CHANNEL_MAX_CBOR_PAYLOAD_SIZE];

    coap_udp_init_message(pkt, COAP_TYPE_NON, NOT_ACCEPTABLE_4_06, mid);
    oc_message_t* message = oc_internal_allocate_outgoing_message();
    if (message)
    {
        memcpy(&message->endpoint, endpoint, sizeof(*endpoint));
        if (token && token_len > 0)
        {
            coap_set_token(pkt, token, token_len);
        }
        struct nx_id nexus_id = {0};
        (void) nexus_oc_wrapper_oc_endpoint_to_nx_id(endpoint, &nexus_id);
        struct nexus_channel_link_security_mode0_data sec_data = {0};
        bool sec_data_exists =
            nexus_channel_link_manager_security_data_from_nxid(&nexus_id,
                                                               &sec_data);
        NEXUS_ASSERT(sec_data_exists,
                     "Unexpectedly attempting to nonce sync for missing "
                     "security link...");

        // this can only happen if we just reset the nonce due to overflow --
        // otherwise, nonce sync should never be triggered. In this case, we
        // want to send a special-value (UINT32_MAX) which is interpreted as
        // 'reset nonce to 0'
        uint32_t sync_nonce = sec_data.nonce;
        if (is_nonce_reset)
        {
            sync_nonce = NEXUS_CHANNEL_LINK_SECURITY_RESET_NONCE_SIGNAL_VALUE;
        }

        // encode the outbound message as secured
        nexus_cose_mac0_common_macparams_t mac_params = {
            &sec_data.sym_key,
            sync_nonce,
            // aad
            {
                NOT_ACCEPTABLE_4_06,
                // no URI, response message
                NULL,
                0,
            },
            // no payload to secure - just method + nonce
            NULL,
            0,
        };

        pkt->payload_len = nexus_oc_wrapper_repack_buffer_secured(
            coap_payload_buffer, sizeof(coap_payload_buffer), &mac_params);
        pkt->payload = coap_payload_buffer;

        // set the header content-format to indicate the payload is
        // secured
        coap_set_header_content_format(pkt, APPLICATION_COSE_MAC0);

        // securely clear the Nexus Channel security data from the
        // stack
        nexus_secure_memclr(
            &sec_data,
            sizeof(struct nexus_channel_link_security_mode0_data),
            sizeof(struct nexus_channel_link_security_mode0_data));

        // transfer from coap packet to message->data
        size_t len = coap_serialize_message(pkt, message->data);
        if (len > 0)
        {
            message->length = len;
            oc_send_message(message);
        }
        if (message->ref_count == 0)
        {
            oc_message_unref(message);
        }
    }
}

#endif // #if NEXUS_CHANNEL_LINK_SECURITY_ENABLED

#if (NEXUS_CHANNEL_OC_ENABLE_EMPTY_RESPONSES_ON_ERROR ||                       \
     NEXUS_CHANNEL_LINK_SECURITY_ENABLED)
// used by Nexus Security Manager to send error responses related to auth
static void coap_send_empty_response(coap_message_type_t type,
                                     uint16_t mid,
                                     const uint8_t* token,
                                     size_t token_len,
                                     uint8_t code,
                                     const oc_endpoint_t* endpoint)
{
    // not actually 'empty' per spec, doesn't have code of 0.00
    OC_DBG("CoAP send 'empty' message: mid=%u, code=%u", mid, code);
    coap_packet_t msg[1]; // empty response
    coap_udp_init_message(msg, type, code, mid);
    oc_message_t* message = oc_internal_allocate_outgoing_message();
    if (message)
    {
        memcpy(&message->endpoint, endpoint, sizeof(*endpoint));
        if (token && token_len > 0)
        {
            coap_set_token(msg, token, token_len);
        }
        size_t len = coap_serialize_message(msg, message->data);
        if (len > 0)
        {
            message->length = len;
            oc_send_message(message);
        }
        if (message->ref_count == 0)
        {
            oc_message_unref(message);
        }
    }
}
#endif // #if (NEXUS_CHANNEL_OC_ENABLE_EMPTY_RESPONSES_ON_ERROR ||
       // NEXUS_CHANNEL_LINK_SECURITY_ENABLED)

#if NEXUS_CHANNEL_LINK_SECURITY_ENABLED
// will modify `msg` in place
// returns `true` if processing of the message should continue and pass
// to the application layer, `false` otherwise
static coap_status_t _coap_unpack_possibly_secured_payload(
    coap_packet_t* coap_pkt,
    const oc_endpoint_t* const sender_endpoint,
    bool* rcvd_pkt_secured)
{
    *rcvd_pkt_secured = false;

    // will modify `coap_pkt->payload` and `coap_pkt.payload_len`
    // if the authentication succeeds. If the message is *not*
    // authenticated *and* the message is a request to a secured
    // resource, this will return a failure.
    const uint8_t* original_payload_ptr = coap_pkt->payload;
    nexus_channel_sm_auth_error_t auth_result =
        nexus_channel_authenticate_message(sender_endpoint, coap_pkt);

    if (coap_pkt->payload != original_payload_ptr)
    {
        // we've repacked the message without security, update
        // the parsed CoAP header, but provide a boolean to let downstream
        // parsers know that the originally received payload was secured
        coap_set_header_content_format(coap_pkt, APPLICATION_VND_OCF_CBOR);
        *rcvd_pkt_secured = true;
    }

    coap_status_t status_code = INTERNAL_SERVER_ERROR_5_00;
    switch (auth_result)
    {
        case NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_VALID_NONCE_SYNC_RECEIVED:
            // The received message will have a CoAP code of 4.06, and the
            // application client callback should resend the original message -
            // we've already updated the local nonce.
            status_code = NEXUS_CHANNEL_SECURITY_RESEND_PREVIOUS_REQUEST;
            break;

        case NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_NONE:
            status_code = COAP_NO_ERROR;
            break;

        case NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_NONCE_APPROACHING_MAX_FORCED_RESET_REQUIRED:
            // signal to the caller that although the message was valid, the
            // nonce for this link is getting too large, nearing overflow - so a
            // special case nonce sync should be sent which 'resets' the nonce
            // of the requester
            status_code =
                NEXUS_CHANNEL_SECURITY_REQUIRE_NONCE_SYNC_SPECIAL_CASE_RESET;
            break;

        case NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_REQUEST_RECEIVED_WITH_INVALID_NONCE:
            // should trigger caller to send nonce sync
            status_code = NOT_ACCEPTABLE_4_06;
            break;

        case NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_SENDER_DEVICE_NOT_LINKED:
            // intentional fallthrough
        case NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_MAC_INVALID:
            status_code = UNAUTHORIZED_4_01;
            break;

        case NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_COSE_UNPARSEABLE:
            // intentional fallthrough
        case NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_PAYLOAD_SIZE_INVALID:
            status_code = BAD_REQUEST_4_00;
            break;

        case NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_RESOURCE_REQUIRES_SECURED_REQUEST:
            status_code = FORBIDDEN_4_03;
            break;

        default:
            NEXUS_ASSERT(false, "Should not reach this case");
            break;
    }

    NEXUS_ASSERT(status_code != INTERNAL_SERVER_ERROR_5_00,
                 "CoAP status code unexpected");

    return status_code;
}
#endif /* NEXUS_CHANNEL_LINK_SECURITY_ENABLED */

/*---------------------------------------------------------------------------*/
/*- Internal API ------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
int coap_receive(oc_message_t* msg)
{
    coap_status_code = COAP_NO_ERROR;

    OC_DBG("CoAP Engine: received datalen=%u from", (unsigned int) msg->length);
    OC_LOGipaddr(msg->endpoint);
    OC_LOGbytes(msg->data, msg->length);

    // static declaration reduces stack peaks and program code size
    static coap_packet_t parsed_coap_pkt[1]; // this way the packet can be
                                             // treated as pointer as usual
    static coap_packet_t response[1];
    static coap_transaction_t* transaction;
    transaction = NULL;

#ifdef OC_CLIENT
    oc_client_cb_t* client_cb = 0;
#endif // OC_CLIENT

    bool rcvd_pkt_secured = false;

    coap_status_code = coap_udp_parse_message(
        parsed_coap_pkt, msg->data, (uint16_t) msg->length);

#if NEXUS_CHANNEL_LINK_SECURITY_ENABLED
    // Only attempt to unpack/authenticate COSE payload if the encapsulating
    // CoAP message was parseable
    if (coap_status_code == COAP_NO_ERROR)
    {
        // may modify `parsed_coap_pkt` in place
        // Either returns a CoAP status code, or indicates whether a previously
        // sent request should be silently resent (we've updated the nonce)
        // Will return `COAP_NO_ERROR` if the CoAP packet should continue
        // to be parsed, and a different code otherwise. Specifically,
        // returning `NOT_ACCEPTABLE_4_06` here indicates that we should
        // send a nonce sync, and `NEXUS_SECURITY_RESEND_PREVIOUS_REQUEST`
        // asks to resecure and resend the previous payload.
        coap_status_code = _coap_unpack_possibly_secured_payload(
            parsed_coap_pkt,
            &((oc_message_t*) msg)->endpoint,
            &rcvd_pkt_secured);
    }
#endif /* NEXUS_CHANNEL_LINK_SECURITY_ENABLED */

    // Here, we pass the message onwards to an appropriate application handler
    // (request or response handler)
    if (coap_status_code == COAP_NO_ERROR)
    {
#ifdef OC_DEBUG
        OC_DBG("  Parsed: CoAP version: %u, token: 0x%02X%02X, mid: %u",
               parsed_coap_pkt->version,
               parsed_coap_pkt->token[0],
               parsed_coap_pkt->token[1],
               parsed_coap_pkt->mid);
        switch (parsed_coap_pkt->type)
        {
            case COAP_TYPE_CON:
                OC_DBG("  type: CON");
                break;
            case COAP_TYPE_NON:
                OC_DBG("  type: NON");
                break;
            case COAP_TYPE_ACK:
                OC_DBG("  type: ACK");
                break;
            case COAP_TYPE_RST:
                OC_DBG("  type: RST");
                break;
            default:
                break;
        }
#endif
        transaction = coap_get_transaction_by_mid(parsed_coap_pkt->mid);
        if (transaction)
        {
            coap_clear_transaction(transaction);
        }
        transaction = NULL;

        // handle requests
        if (parsed_coap_pkt->code >= COAP_GET &&
            parsed_coap_pkt->code <= COAP_DELETE)
        {

#ifdef OC_DEBUG
            switch (parsed_coap_pkt->code)
            {
                case COAP_GET:
                    OC_DBG("  method: GET");
                    break;
                case COAP_PUT:
                    OC_DBG("  method: PUT");
                    break;
                case COAP_POST:
                    OC_DBG("  method: POST");
                    break;
                case COAP_DELETE:
                    OC_DBG("  method: DELETE");
                    break;
            }
            OC_DBG("  URL: %.*s",
                   (int) parsed_coap_pkt->uri_path_len,
                   parsed_coap_pkt->uri_path);
            OC_DBG("  QUERY: %.*s",
                   (int) parsed_coap_pkt->uri_query_len,
                   parsed_coap_pkt->uri_query);
            OC_DBG("  Payload: %.*s",
                   (int) parsed_coap_pkt->payload_len,
                   parsed_coap_pkt->payload);
#endif
            const char* href;
            size_t href_len = coap_get_header_uri_path(parsed_coap_pkt, &href);
            {
                if (parsed_coap_pkt->type == COAP_TYPE_CON)
                {
                    coap_udp_init_message(response,
                                          COAP_TYPE_ACK,
                                          CONTENT_2_05,
                                          parsed_coap_pkt->mid);
                }
                else
                {
#if NEXUS_CHANNEL_OC_ENABLE_DUPLICATE_MESSAGE_ID_CHECK
                    if (check_if_duplicate(parsed_coap_pkt->mid,
                                           (uint8_t) msg->endpoint.device))
                    {
                        OC_DBG("Detected duplicate, returning early...");
                        return 0;
                    }
                    history[idx] = parsed_coap_pkt->mid;
                    history_dev[idx] = (uint8_t) msg->endpoint.device;
                    idx = (idx + 1) % OC_REQUEST_HISTORY_SIZE;
#endif
                    if (href_len == 7 && memcmp(href, "oic/res", 7) == 0)
                    {
                        coap_udp_init_message(response,
                                              COAP_TYPE_CON,
                                              CONTENT_2_05,
                                              coap_get_mid());
                    }
                    else
                    {
                        coap_udp_init_message(response,
                                              COAP_TYPE_NON,
                                              CONTENT_2_05,
                                              coap_get_mid());
                    }
                }
            }

            /* create transaction for response */
            transaction = coap_new_transaction(response->mid, &msg->endpoint);

            if (transaction)
            {
                // incoming requests are handled here
                if (oc_ri_invoke_coap_entity_handler(
                        parsed_coap_pkt,
                        response,
                        transaction->message->data + COAP_MAX_HEADER_SIZE,
                        &msg->endpoint))
                {
                    OC_DBG("CoAP response type=%u", response->code);
                }
                if (response->code != 0)
                {
                    goto send_message;
                }
            }
        }
        else
        {
            // Incoming response (not request) message
            OC_DBG("Handling response message");

#ifdef OC_CLIENT
            if (parsed_coap_pkt->type != COAP_TYPE_RST)
            {
                client_cb = oc_ri_find_client_cb_by_token(
                    parsed_coap_pkt->token, parsed_coap_pkt->token_len);
            }

            if (client_cb)
            {
    #if NEXUS_CHANNEL_LINK_SECURITY_ENABLED
                if (client_cb->nx_request_secured && !rcvd_pkt_secured)
                {
                    // if the request was secured and the
                    // reply was not, then ignore this
                    // message
                    OC_WRN("received unsecured reply to secured request; "
                           "dropping message");
                    return UNAUTHORIZED_4_01;
                }
    #endif
                // responses to a request are handled here
                OC_DBG("calling oc_ri_invoke_client_cb");

                oc_ri_invoke_client_cb(
                    parsed_coap_pkt, client_cb, &msg->endpoint);
            }
#endif // OC_CLIENT
        }
    }
#if NEXUS_CHANNEL_LINK_SECURITY_ENABLED
    // we received a secured request that should not be processed by
    // resource handlers, and we should send a nonce sync message back to the
    // requester instead. (This `coap_status_code` is the result of parsing,
    // not the actual CoAP code of the received message).
    else if (coap_status_code == NOT_ACCEPTABLE_4_06)
    {
        OC_DBG("Sending Nonce Sync response, mid=%d", parsed_coap_pkt->mid);
        coap_send_nonce_sync_response(parsed_coap_pkt->mid,
                                      parsed_coap_pkt->token,
                                      parsed_coap_pkt->token_len,
                                      &((oc_message_t*) msg)->endpoint,
                                      false);
        // the above will result in a message being posted
        // on the outbound queue, request processing to send
        nxp_common_request_processing();
        // we won't attempt to receive the message, we want to
        // nonce sync.
        return coap_status_code;
    }
    else if (coap_status_code == NEXUS_CHANNEL_SECURITY_RESEND_PREVIOUS_REQUEST)
    {
        OC_DBG("Resending previous secured request with updated security info");
        // will look up the previous transaction to this endpoint
        coap_send_previous_request_with_repacked_security(
            parsed_coap_pkt->mid, &((oc_message_t*) msg)->endpoint);
        nxp_common_request_processing();
        return coap_status_code;
    }
    else if (coap_status_code ==
             NEXUS_CHANNEL_SECURITY_REQUIRE_NONCE_SYNC_SPECIAL_CASE_RESET)
    {
        OC_DBG("Sending RESET Nonce Sync response, mid=%d",
               parsed_coap_pkt->mid);
        coap_send_nonce_sync_response(parsed_coap_pkt->mid,
                                      parsed_coap_pkt->token,
                                      parsed_coap_pkt->token_len,
                                      &((oc_message_t*) msg)->endpoint,
                                      true);
        // the above will result in a message being posted
        // on the outbound queue, request processing to send
        nxp_common_request_processing();
        // we won't attempt to receive the message, we want to
        // nonce sync.
        return coap_status_code;
    }
#endif /* NEXUS_CHANNEL_LINK_SECURITY_ENABLED */
    else
    {
        OC_ERR("Unexpected CoAP command");
#if (NEXUS_CHANNEL_OC_ENABLE_EMPTY_RESPONSES_ON_ERROR ||                       \
     NEXUS_CHANNEL_LINK_SECURITY_ENABLED)
        coap_send_empty_response(parsed_coap_pkt->type == COAP_TYPE_CON ?
                                     COAP_TYPE_ACK :
                                     COAP_TYPE_NON,
                                 parsed_coap_pkt->mid,
                                 parsed_coap_pkt->token,
                                 parsed_coap_pkt->token_len,
                                 coap_status_code,
                                 &msg->endpoint);
        nxp_common_request_processing();
#endif // #if (NEXUS_CHANNEL_OC_ENABLE_EMPTY_RESPONSES_ON_ERROR ||
       // NEXUS_CHANNEL_LINK_SECURITY_ENABLED)
        return coap_status_code;
    }

send_message:
    OC_DBG("entering `send_message`");
    if (coap_status_code == CLEAR_TRANSACTION)
    {
        coap_clear_transaction(transaction);
    }
    else if (transaction)
    {
        if (response->type != COAP_TYPE_RST && parsed_coap_pkt->token_len)
        {
            if (parsed_coap_pkt->code >= COAP_GET &&
                parsed_coap_pkt->code <= COAP_DELETE)
            {
                coap_set_token(response,
                               parsed_coap_pkt->token,
                               parsed_coap_pkt->token_len);
            }

#if NEXUS_CHANNEL_LINK_SECURITY_ENABLED
            bool resource_secured = false;
            bool sec_data_exists = false;

            // send secured replies to requests to resource methods that are
            // secured
            const char* href;
            size_t href_len = coap_get_header_uri_path(parsed_coap_pkt, &href);
            oc_resource_t* res = oc_ri_get_app_resource_by_uri(
                href, href_len, NEXUS_CHANNEL_NEXUS_DEVICE_ID);
            // NOTE: This function is only used by servers, to determine
            // if a resource/method combination it is serving requires
            // security to access or not.
            resource_secured = nexus_channel_sm_resource_method_is_secured(
                res, (oc_method_t) parsed_coap_pkt->code);

            // get Nexus ID
            struct nx_id nexus_id = {0};
            (void) nexus_oc_wrapper_oc_endpoint_to_nx_id(
                &transaction->message->endpoint, &nexus_id);

            // get security data
            struct nexus_channel_link_security_mode0_data sec_data = {0};
            sec_data_exists =
                nexus_channel_link_manager_security_data_from_nxid(&nexus_id,
                                                                   &sec_data);

            // Clients may make secured or unsecured requests - we
            // only respond with a secured response if:
            // * We have a secured link to the client AND
            // * (the requested resource/method is secured OR
            //   the client sent a secure request (regardless of
            //   resource/method security))
            if (sec_data_exists && (resource_secured || rcvd_pkt_secured))
            {
                // encode the outbound message as secured
                nexus_cose_mac0_common_macparams_t mac_params = {
                    &sec_data.sym_key,
                    sec_data.nonce,
                    // aad
                    {
                        (uint8_t) response->code,
                        // response has no URI
                        NULL,
                        0,
                    },
                    response->payload,
                    response->payload_len,
                };

                // so that we don't memcpy over the payload we are
                // encoding
                uint8_t tmp_output[NEXUS_CHANNEL_MAX_CBOR_PAYLOAD_SIZE];

                const size_t new_payload_size =
                    nexus_oc_wrapper_repack_buffer_secured(
                        tmp_output, sizeof(tmp_output), &mac_params);
                response->payload_len = new_payload_size;
                memcpy(response->payload, tmp_output, new_payload_size);

                // set the header content-format to indicate the payload is
                // secured
                if (new_payload_size > 0)
                {
                    coap_set_header_content_format(response,
                                                   APPLICATION_COSE_MAC0);
                }
                else
                {
                    OC_WRN("Secured server message cannot be packed");
                    coap_clear_transaction(transaction);
                }

                // securely clear the Nexus Channel security data from the
                // stack
                nexus_secure_memclr(
                    &sec_data,
                    sizeof(struct nexus_channel_link_security_mode0_data),
                    sizeof(struct nexus_channel_link_security_mode0_data));
            }
#else
            // only used if security is enabled
            (void) rcvd_pkt_secured;
#endif
            // Nexus security currently repacks message, changing size
            transaction->message->length =
                coap_serialize_message(response, transaction->message->data);

            if (transaction->message->length == 0)
            {
                coap_clear_transaction(transaction);
            }
            else
            {
                coap_send_transaction(transaction, false);
            }
        }
    }

    return coap_status_code;
}
/*---------------------------------------------------------------------------*/
void coap_init_engine(void)
{
    coap_register_as_transaction_handler();
}
/*---------------------------------------------------------------------------*/
OC_PROCESS_THREAD(coap_engine, ev, data)
{
    OC_PROCESS_BEGIN();

    coap_register_as_transaction_handler();
    coap_init_connection();

    while (1)
    {
        OC_PROCESS_YIELD();

        if (ev == oc_events[INBOUND_RI_EVENT])
        {
            OC_DBG("Handling INBOUND_RI_EVENT\n");
            coap_receive((oc_message_t*) data);
            oc_message_unref((oc_message_t*) data);
        }
        else if (ev == OC_PROCESS_EVENT_TIMER)
        {
            coap_check_transactions();
        }
    }

    OC_PROCESS_END();
}
/*---------------------------------------------------------------------------*/

#pragma GCC diagnostic pop
