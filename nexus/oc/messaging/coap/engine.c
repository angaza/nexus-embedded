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
#include "oc/include/nexus_channel_security.h"
#include "oc_api.h"
#include "oc_buffer.h"
#include "src/nexus_channel_sm.h"
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
/*
#ifdef OC_TCP
#include "coap_signal.h"
#endif
*/
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
// always a fixed code of 4.06
static void coap_send_nonce_sync_response(uint16_t mid,
                                          const uint8_t* token,
                                          size_t token_len,
                                          oc_endpoint_t* endpoint)
{
    coap_packet_t pkt[1];
    // will be populated with repacked COSE MAC0 data
    uint8_t coap_payload_buffer[OC_BLOCK_SIZE];

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

        // populate COSE_MAC0 struct
        nexus_security_mode0_cose_mac0_t cose_mac0 = {0};
        cose_mac0.protected_header_method = NOT_ACCEPTABLE_4_06;
        cose_mac0.protected_header_nonce = sec_data.nonce;
        cose_mac0.kid = 0;
        cose_mac0.payload_len = 0;

        // compute MAC
        struct nexus_check_value mac =
            _nexus_channel_sm_compute_mac_mode0(&cose_mac0, &sec_data);
        cose_mac0.mac = &mac;

        // repack the message stored in the transaction with secured
        // data

        pkt->payload_len = nexus_oc_wrapper_repack_buffer_secured(
            coap_payload_buffer, OC_BLOCK_SIZE, &cose_mac0);
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
            coap_send_message(message);
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
                                     oc_endpoint_t* endpoint)
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
            coap_send_message(message);
        }
        if (message->ref_count == 0)
        {
            oc_message_unref(message);
        }
    }
}
#endif // #if (NEXUS_CHANNEL_OC_ENABLE_EMPTY_RESPONSES_ON_ERROR ||
       // NEXUS_CHANNEL_LINK_SECURITY_ENABLED)
/*---------------------------------------------------------------------------*/
/*- Internal API ------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
int coap_receive(oc_message_t* msg, bool request_secured)
{
    coap_status_code = COAP_NO_ERROR;

    OC_DBG("CoAP Engine: received datalen=%u from", (unsigned int) msg->length);
    OC_LOGipaddr(msg->endpoint);
    OC_LOGbytes(msg->data, msg->length);

    // static declaration reduces stack peaks and program code size
    static coap_packet_t
        message[1]; // this way the packet can be treated as pointer as usual
    static coap_packet_t response[1];
    static coap_transaction_t* transaction;
    transaction = NULL;

#ifdef OC_CLIENT
    oc_client_cb_t* client_cb = 0;
#endif // OC_CLIENT

    {
        coap_status_code =
            coap_udp_parse_message(message, msg->data, (uint16_t) msg->length);
    }

    if (coap_status_code == COAP_NO_ERROR)
    {
#ifdef OC_DEBUG
        OC_DBG("  Parsed: CoAP version: %u, token: 0x%02X%02X, mid: %u",
               message->version,
               message->token[0],
               message->token[1],
               message->mid);
        switch (message->type)
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
        transaction = coap_get_transaction_by_mid(message->mid);
        if (transaction)
        {
            coap_clear_transaction(transaction);
        }
        transaction = NULL;

        // handle requests
        if (message->code >= COAP_GET && message->code <= COAP_DELETE)
        {

#ifdef OC_DEBUG
            switch (message->code)
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
            OC_DBG(
                "  URL: %.*s", (int) message->uri_path_len, message->uri_path);
            OC_DBG("  QUERY: %.*s",
                   (int) message->uri_query_len,
                   message->uri_query);
            OC_DBG("  Payload: %.*s",
                   (int) message->payload_len,
                   message->payload);
#endif
            const char* href;
            size_t href_len = coap_get_header_uri_path(message, &href);
            /*
            #ifdef OC_TCP
                  if (msg->endpoint.flags & TCP) {
                    coap_tcp_init_message(response, CONTENT_2_05);
                  } else
            #endif // OC_TCP
            */
            {
                if (message->type == COAP_TYPE_CON)
                {
                    coap_udp_init_message(
                        response, COAP_TYPE_ACK, CONTENT_2_05, message->mid);
                }
                else
                {
#if NEXUS_CHANNEL_OC_ENABLE_DUPLICATE_MESSAGE_ID_CHECK
                    if (check_if_duplicate(message->mid,
                                           (uint8_t) msg->endpoint.device))
                    {
                        OC_DBG("Detected duplicate, returning early...");
                        return 0;
                    }
                    history[idx] = message->mid;
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
                        message,
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
            if (message->type != COAP_TYPE_RST)
            {
                client_cb = oc_ri_find_client_cb_by_token(message->token,
                                                          message->token_len);
            }

            if (client_cb)
            {
                // responses to a request are handled here
                OC_DBG("calling oc_ri_invoke_client_cb");
                oc_ri_invoke_client_cb(message, client_cb, &msg->endpoint);
            }
#endif // OC_CLIENT
        }
    }
    else
    {
        OC_ERR("Unexpected CoAP command");
#if NEXUS_CHANNEL_OC_ENABLE_EMPTY_RESPONSES_ON_ERROR
        coap_send_empty_response(
            message->type == COAP_TYPE_CON ? COAP_TYPE_ACK : COAP_TYPE_NON,
            message->mid,
            message->token,
            message->token_len,
            coap_status_code,
            &msg->endpoint);
#endif // #if NEXUS_CHANNEL_OC_ENABLE_EMPTY_RESPONSES_ON_ERROR
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
        if (response->type != COAP_TYPE_RST && message->token_len)
        {
            if (message->code >= COAP_GET && message->code <= COAP_DELETE)
            {
                coap_set_token(response, message->token, message->token_len);
            }
            transaction->message->length =
                coap_serialize_message(response, transaction->message->data);

            if (transaction->message->length > 0)
            {
#if NEXUS_CHANNEL_LINK_SECURITY_ENABLED
                bool resource_secured = false;
                bool sec_data_exists = false;

                // send secured replies to requests to resource methods that are
                // secured
                const char* href;
                size_t href_len = coap_get_header_uri_path(message, &href);
                oc_resource_t* res = oc_ri_get_app_resource_by_uri(
                    href, href_len, NEXUS_CHANNEL_NEXUS_DEVICE_ID);
                // NOTE: This function is only used by servers, to determine
                // if a resource/method combination it is serving requires
                // security to access or not.
                resource_secured = nexus_channel_sm_resource_method_is_secured(
                    res, (oc_method_t) message->code);

                // get Nexus ID
                struct nx_id nexus_id = {0};
                (void) nexus_oc_wrapper_oc_endpoint_to_nx_id(
                    &transaction->message->endpoint, &nexus_id);

                // get security data
                struct nexus_channel_link_security_mode0_data sec_data = {0};
                sec_data_exists =
                    nexus_channel_link_manager_security_data_from_nxid(
                        &nexus_id, &sec_data);

                // Clients may make secured or unsecured requests - we
                // only respond with a secured response if:
                // * We have a secured link to the client AND
                // * (the requested resource/method is secured OR
                //   the client sent a secure request (regardless of
                //   resource/method security))
                if (sec_data_exists && (resource_secured || request_secured))
                {
                    // populate COSE_MAC0 struct
                    nexus_security_mode0_cose_mac0_t cose_mac0 = {0};

                    // the `code` field is shared between requests and responses
                    cose_mac0.protected_header_method =
                        (uint8_t) response->code;
                    cose_mac0.protected_header_nonce = sec_data.nonce;
                    cose_mac0.kid = 0;
                    // also sets the payload pointer of `cose_mac0`
                    cose_mac0.payload_len = coap_get_payload(
                        response, (const uint8_t**) &cose_mac0.payload);

                    // compute MAC
                    struct nexus_check_value mac =
                        _nexus_channel_sm_compute_mac_mode0(&cose_mac0,
                                                            &sec_data);
                    cose_mac0.mac = &mac;

                    // repack the message stored in the response struct
                    // with the secured response
                    const uint8_t old_payload_size = response->payload_len;
                    const uint8_t new_payload_size =
                        nexus_oc_wrapper_repack_buffer_secured(
                            response->payload, OC_BLOCK_SIZE, &cose_mac0);
                    response->payload_len = new_payload_size;
                    NEXUS_ASSERT(
                        new_payload_size >= old_payload_size,
                        "Secured message smaller than unsecured payload...");

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
                (void) request_secured;
#endif
                // Nexus security currently repacks message, changing size
                transaction->message->length = coap_serialize_message(
                    response, transaction->message->data);
                coap_send_transaction(transaction);
            }
            else
            {
                coap_clear_transaction(transaction);
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
            bool request_secured = false;
#if NEXUS_CHANNEL_LINK_SECURITY_ENABLED
            coap_packet_t coap_pkt[1];

            // make a local copy of the message for security manager processing;
            // coap_merge_multi_option in the parsing code makes it impossible
            // to parse some headers multiple times (e.g., headers with
            // multiple uri-path options)
            oc_message_t message = {0};
            message.length = ((oc_message_t*) data)->length;
            memcpy(message.data, ((oc_message_t*) data)->data, message.length);
            oc_endpoint_copy(&message.endpoint,
                             &((oc_message_t*) data)->endpoint);

            coap_status_code =
                coap_udp_parse_message(coap_pkt, message.data, message.length);

            // standalone, does not interact with the next if block
            if (coap_status_code == COAP_NO_ERROR)
            {
                unsigned int format = 0;
                coap_get_header_content_format((void*) coap_pkt, &format);
                if (format == APPLICATION_COSE_MAC0)
                {
                    request_secured = true;
                }
            }

            // CoAP parsed correctly, attempt to authenticate
            if (coap_status_code != COAP_NO_ERROR)
            {
                // do nothing, `coap_receive` will fail to process the message
                oc_message_unref((oc_message_t*) data);
                continue;
            }
            else if (coap_pkt->code == NOT_ACCEPTABLE_4_06)
            {
                // CLIENT receives a 'nonce-sync' in response to a previous
                // request This case should *never* be entered in an unsolicited
                // manner (this should *only* occur if `coap_pkt` is a response
                // to a request previously made by this device to another device
                // via a secured Nexus Channel link and nonce was not current)
                // authenticate message with Nexus Channel
                // `nexus_channel_authenticate_message` will update nonce
                // if the message is valid
                coap_status_code = nexus_channel_authenticate_message(
                    &message.endpoint, coap_pkt);
                // we've updated the nonce, no further processing
                // is required on this message
                // XXX do we want to automatically 'resend' the originally
                // sent message that triggered this nonce sync?
                oc_message_unref((oc_message_t*) data);
                continue;
            }

            else if (coap_pkt->code >= COAP_GET &&
                     coap_pkt->code <= COAP_DELETE)
            {
                // SERVER receives a request message
                coap_status_code = nexus_channel_authenticate_message(
                    &message.endpoint, coap_pkt);

                // case if nonce is invalid/doesn't match
                // Implies that a link does exist
                if (coap_status_code == NOT_ACCEPTABLE_4_06)
                {
                    coap_send_nonce_sync_response(coap_pkt->mid,
                                                  coap_pkt->token,
                                                  coap_pkt->token_len,
                                                  &message.endpoint);
                    // the above will result in a message being posted
                    // on the outbound queue, request processing to send
                    nxp_common_request_processing();
                    oc_message_unref((oc_message_t*) data);
                    // continue, we don't want to attempt to 'receive'
                    // the message, it was a nonce sync.
                    continue;
                }

                // We use 'empty' responses to handle security-layer
                // failures *other than* nonce sync (4.06).
                // In reality, this isn't an 'empty' response
                // as the CoAP spec defines it, since we include an
                // error code (truly 'empty' responses are code 0.00)
                else if (coap_status_code != COAP_NO_ERROR)
                {
                    coap_send_empty_response(coap_pkt->type == COAP_TYPE_CON ?
                                                 COAP_TYPE_ACK :
                                                 COAP_TYPE_NON,
                                             coap_pkt->mid,
                                             coap_pkt->token,
                                             coap_pkt->token_len,
                                             coap_status_code,
                                             &message.endpoint);
                    // the above will result in a message being posted on
                    // the outbound queue; request processing to send
                    nxp_common_request_processing();
                    oc_message_unref((oc_message_t*) data);
                    continue;
                }
                // if there is a parsing error, then this will be handled in
                // `coap_receive`
            }
            else if (coap_pkt->code >= CREATED_2_01)
            {
                // CLIENT receives a response message to a previous request
                if (nexus_channel_authenticate_message(
                        &message.endpoint, coap_pkt) != COAP_NO_ERROR)
                {
                    // if there is any security issue in the incoming message,
                    // ignore it and continue.
                    // no response is sent, no need for requesting processing
                    OC_DBG("Client received secured response, but does not "
                           "authenticate. Ignoring.");
                    oc_message_unref((oc_message_t*) data);
                    continue;
                }
            }
            // Before passing the incoming message to `coap_receive`, if it was
            // secured, repack it *without* security.
            coap_packet_t coap_pkt_repacked[1];

            // reuse temporary local message struct
            // need to recopy the message data, but don't need to update the
            // message endpoint - it hasn't changed
            message.length = ((oc_message_t*) data)->length;
            memcpy(message.data, ((oc_message_t*) data)->data, message.length);
            coap_status_code = coap_udp_parse_message(
                coap_pkt_repacked, message.data, message.length);

            // will not erase existing data in payload beyond `payload_len`
            // which is OK
            const uint8_t old_payload_len = coap_pkt_repacked->payload_len;
            uint8_t new_payload_len = old_payload_len;
            if (nexus_oc_wrapper_extract_embedded_payload_from_mac0_payload(
                    coap_pkt_repacked->payload,
                    old_payload_len,
                    &new_payload_len))
            {
                coap_pkt_repacked->payload_len = new_payload_len;

                // reserialize the 'unpacked' message into data buffer
                ((oc_message_t*) data)->length = coap_serialize_message(
                    coap_pkt_repacked, ((oc_message_t*) data)->data);
            }
            NEXUS_ASSERT(new_payload_len <= old_payload_len,
                         "Unexpected - unsecured payload larger than secured");

#endif /* NEXUS_CHANNEL_LINK_SECURITY_ENABLED */
            coap_receive((oc_message_t*) data, request_secured);

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
