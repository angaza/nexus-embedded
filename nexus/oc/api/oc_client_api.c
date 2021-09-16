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
//
// Modifications (c) 2020 Angaza, Inc.
*/

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include "messaging/coap/coap.h"
#include "messaging/coap/transactions.h"
#include "src/nexus_channel_sm.h"
#include "src/nexus_oc_wrapper.h"
#include "src/nexus_security.h"

#include "oc_api.h"

#if OC_CLIENT

static coap_transaction_t *transaction;
coap_packet_t request[1];
oc_client_cb_t *client_cb;

oc_event_callback_retval_t oc_ri_remove_client_cb(void *data);

// helper method for dispatch_coap_request to handle security
// checking and packing of payload
#if NEXUS_CHANNEL_LINK_SECURITY_ENABLED
static bool _prepare_secured_coap_request(uint8_t* const transaction_payload, uint8_t* payload_size)
{

  bool sec_data_exists = false;

  // get Nexus ID of destination/server
  struct nx_id nexus_id = {0};
  nexus_oc_wrapper_oc_endpoint_to_nx_id(&client_cb->endpoint, &nexus_id);

  // get security data for link to server
  struct nexus_channel_link_security_mode0_data sec_data = {0};
  sec_data_exists = nexus_channel_link_manager_security_data_from_nxid(
          &nexus_id, &sec_data);

  // if requested method is secured but we have no security data, return early
  // with error - there is no way to properly secure the request
  if (!sec_data_exists)
  {
    OC_WRN("Requested secured method but no security data available!");
    return false;
  }

  // Now, we have the security data for the secured link to the server, and
  // can begin building the secured request message.

  // AAD computed over URI without terminating byte
  uint8_t uri_size = 0;
  if (client_cb->uri.size > 0)
  {
      uri_size = oc_string_len(client_cb->uri);
  }
  // populate COSE_MAC0 struct. Make every outbound request with the
  // *current* nonce of the link + 1
    nexus_cose_mac0_common_macparams_t mac_params = {
        &sec_data.sym_key,
        sec_data.nonce + 1,
        // aad
        {
            client_cb->method,
            (uint8_t*) client_cb->uri.ptr,
            uri_size,
        },
        // the CBOR payload has been written here by the client application
        transaction_payload,
        *payload_size,
    };

    // size of buffer for the payload data based on structs/constants defined elsewhere
    // We are assuming that `transaction_payload` is pointing to the payload
    // part of an `oc_message_t` struct inside a `coap_transaction_t`.
    NEXUS_STATIC_ASSERT(sizeof(((oc_message_t*) 0)->data) - COAP_MAX_HEADER_SIZE <= NEXUS_CHANNEL_MAX_COAP_TOTAL_MESSAGE_SIZE, "Transaction payload buffer size is larger than local buffer");
    // will be populated with repacked COSE MAC0 data
    uint8_t coap_payload_buffer[NEXUS_CHANNEL_MAX_CBOR_PAYLOAD_SIZE];

  // `nexus_oc_wrapper_repack_buffer_secured` internally copies the data from `mac_params->payload`
  // into a local buffer before moving the final 'packed' result back into `coap_payload_buffer`.
  *payload_size = (uint8_t) nexus_oc_wrapper_repack_buffer_secured(coap_payload_buffer, sizeof(coap_payload_buffer), &mac_params);

  NEXUS_ASSERT(*payload_size <= NEXUS_CHANNEL_MAX_CBOR_PAYLOAD_SIZE, "Secured payload size too large");

  if (*payload_size == 0)
  {
    OC_WRN("Secured client message cannot be packed");
    return false;
  }

  // securely clear the Nexus Channel security data from the stack
  nexus_secure_memclr(&sec_data,
            sizeof(struct nexus_channel_link_security_mode0_data),
            sizeof(struct nexus_channel_link_security_mode0_data));

  coap_set_header_content_format(request, APPLICATION_COSE_MAC0);
  // copy secured message into outbound transaction payload buffer

  memcpy(transaction_payload, coap_payload_buffer, *payload_size);

  return true;
}
#endif /* NEXUS_CHANNEL_LINK_SECURITY_ENABLED */

static bool
dispatch_coap_request(bool nx_secure_request)
{
  // initial payload size of packed application CBOR data
  uint8_t payload_size = (uint8_t) oc_rep_get_encoded_payload_size();
  // pointer to where the payload bytes start in the outbound message
  uint8_t* const transaction_payload = transaction->message->data + COAP_MAX_HEADER_SIZE;
  NEXUS_STATIC_ASSERT(COAP_MAX_HEADER_SIZE + NEXUS_CHANNEL_MAX_CBOR_PAYLOAD_SIZE <= NEXUS_CHANNEL_MAX_COAP_TOTAL_MESSAGE_SIZE,
          "Header and payload sizes don't fit within a message");

#if NEXUS_CHANNEL_LINK_SECURITY_ENABLED
  // returns true if the request should be secured and it was successfully secured, or if
  // no security is required and none is applied. Else returns false
  bool security_ok = true;
  if (nx_secure_request)
  {
      // remember that request is secured so that we can
      // check that reply is also secured
      client_cb->nx_request_secured = true;
      security_ok = _prepare_secured_coap_request(transaction_payload, &payload_size);
  }
#endif /* NEXUS_CHANNEL_LINK_SECURITY_ENABLED */

  // add the payload (unsecured POST or COSE_MAC0 for secured GET/POST)
  // and update content-format option
  if (payload_size > 0)
  {
    coap_set_payload(request, transaction_payload, payload_size);

    // set content-format if not already set by security in
    // `_prepare_secured_coap_request`
    if (request->content_format != APPLICATION_COSE_MAC0)
    {
        coap_set_header_content_format(request, APPLICATION_VND_OCF_CBOR);
    }
  }

  bool success = false;
  transaction->message->length =
    coap_serialize_message(request, transaction->message->data);
#if NEXUS_CHANNEL_LINK_SECURITY_ENABLED
  if (transaction->message->length > 0 && security_ok) {
    coap_send_transaction(transaction, nx_secure_request);
#else
  if (transaction->message->length > 0) {
    coap_send_transaction(transaction, false);
#endif



#if NEXUS_CHANNEL_USE_OC_OBSERVABILITY_AND_CONFIRMABLE_COAP_APIS
    if (client_cb->observe_seq == -1) {
      if (client_cb->qos == LOW_QOS)
        oc_set_delayed_callback(client_cb, &oc_ri_remove_client_cb,
                                OC_NON_LIFETIME);
      else
        oc_set_delayed_callback(client_cb, &oc_ri_remove_client_cb,
                                OC_EXCHANGE_LIFETIME);
    }
#else
    // All callbacks should be removed eventually in the event of no response
    oc_set_delayed_callback(client_cb, &oc_ri_remove_client_cb, OC_NON_LIFETIME);
    OC_DBG("Clearing client CB with MID %d after %d seconds",
          (int) client_cb->mid,
          (int) OC_NON_LIFETIME);

#endif // NEXUS_CHANNEL_USE_OC_OBSERVABILITY_AND_CONFIRMABLE_COAP_APIS

    success = true;
  } else {
    OC_WRN(
        "oc_client_api: Failed to send transaction (length %u)",
        transaction->message->length
    );
    coap_clear_transaction(transaction);
    oc_ri_remove_client_cb(client_cb);
  }

  transaction = NULL;
  client_cb = NULL;

  return success;
}

static bool prepare_coap_request(oc_client_cb_t *cb)
{
  coap_message_type_t type = COAP_TYPE_NON;

  if (cb->qos == HIGH_QOS) {
    type = COAP_TYPE_CON;
  }

  transaction = coap_new_transaction(cb->mid, &cb->endpoint);

  if (!transaction) {
    // also free the client callback early here.
    oc_ri_remove_client_cb(cb);
    return false;
  }

  oc_rep_new(transaction->message->data + COAP_MAX_HEADER_SIZE, NEXUS_CHANNEL_MAX_CBOR_PAYLOAD_SIZE);

  coap_udp_init_message(request, type, (uint8_t) cb->method, cb->mid);

  coap_set_token(request, cb->token, cb->token_len);

  coap_set_header_uri_path(request, oc_string(cb->uri), oc_string_len(cb->uri));

#if !NEXUS_CHANNEL_USE_OC_OBSERVABILITY_AND_CONFIRMABLE_COAP_APIS
  if (cb->observe_seq != -1)
  {
    OC_WRN("Observe is not supported but callback has observe_seq set");
    //coap_set_header_observe(request, cb->observe_seq);
  }
#endif // NEXUS_CHANNEL_USE_OC_OBSERVABILITY_AND_CONFIRMABLE_COAP_APIS

  if (oc_string_len(cb->query) > 0) {
    coap_set_header_uri_query(request, oc_string(cb->query));
  }

  client_cb = cb;

  return true;
}

bool
oc_do_get(const char *uri, const bool nx_secure_request, oc_endpoint_t *endpoint, const char *query,
          oc_response_handler_t handler, oc_qos_t qos, void *user_data)
{
  // don't handle discovery for now
  oc_client_handler_t client_handler = {0};
  client_handler.response = handler;

  oc_client_cb_t *cb = oc_ri_alloc_client_cb(uri, endpoint, OC_GET, query,
                                             client_handler, qos, user_data);
  if (!cb)
    return false;

  bool status = false;

  status = prepare_coap_request(cb);

  if (status)
    status = dispatch_coap_request(nx_secure_request);

  return status;
}

bool
oc_init_post(const char *uri, oc_endpoint_t *endpoint, const char *query,
             oc_response_handler_t handler, oc_qos_t qos, void *user_data)
{
  // don't handle discovery for now
  oc_client_handler_t client_handler = {0};
  client_handler.response = handler;

  oc_client_cb_t *cb = oc_ri_alloc_client_cb(uri, endpoint, OC_POST, query,
                                             client_handler, qos, user_data);
  if (!cb) {
    return false;
  }

  return prepare_coap_request(cb);
}

bool
oc_do_post(const bool nx_secure_request)
{
  return dispatch_coap_request(nx_secure_request);
}

#if NEXUS_CHANNEL_USE_OC_OBSERVABILITY_AND_CONFIRMABLE_COAP_APIS
bool
oc_do_observe(const char *uri, oc_endpoint_t *endpoint, const char *query,
              oc_response_handler_t handler, oc_qos_t qos, void *user_data)
{
  oc_client_handler_t client_handler;
  client_handler.response = handler;

  oc_client_cb_t *cb = oc_ri_alloc_client_cb(uri, endpoint, OC_GET, query,
                                             client_handler, qos, user_data);
  if (!cb)
    return false;

  cb->observe_seq = 0;

  bool status = false;

  status = prepare_coap_request(cb);

  if (status)
    status = dispatch_coap_request(false);

  return status;
}

bool
oc_stop_observe(const char *uri, oc_endpoint_t *endpoint)
{
  oc_client_cb_t *cb = oc_ri_get_client_cb(uri, endpoint, OC_GET);

  if (!cb)
    return false;

  cb->mid = coap_get_mid();
  cb->observe_seq = 1;

  bool status = false;

  status = prepare_coap_request(cb);

  if (status)
    status = dispatch_coap_request(false);

  return status;
}
#endif // NEXUS_CHANNEL_USE_OC_OBSERVABILITY_AND_CONFIRMABLE_COAP_APIS

#endif // OC_CLIENT
#pragma GCC diagnostic pop
