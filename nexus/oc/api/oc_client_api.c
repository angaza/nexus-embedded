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
  // size of buffer for the payload data based on structs/constants defined elsewhere
  const uint16_t transaction_payload_buf_size = sizeof(((oc_message_t*) 0)->data) - COAP_MAX_HEADER_SIZE;
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

  // populate COSE_MAC0 struct
  nexus_security_mode0_cose_mac0_t cose_mac0 = {0};
  cose_mac0.protected_header_method = (uint8_t) client_cb->method;
  cose_mac0.protected_header_nonce = sec_data.nonce;
  cose_mac0.kid = 0;
  cose_mac0.payload_len = *payload_size;
  // the encoded payload has been written here by the client application
  cose_mac0.payload = transaction_payload;

  // compute MAC
  struct nexus_check_value mac = _nexus_channel_sm_compute_mac_mode0(&cose_mac0, &sec_data);
  cose_mac0.mac = &mac;

  // repack the message stored in the transaction with secured data
  // At this point, we are overwriting `transaction_payload` with `cose_mac0`, but `cose_mac0`s
  // `payload` field is pointing to `transaction_payload`. However, this is OK because
  // `nexus_oc_wrapper_repack_buffer_secured` internally copies the data from `transaction_payload`
  // into a local buffer before moving the final 'packed' result back into `transaction_payload`.
  *payload_size = nexus_oc_wrapper_repack_buffer_secured(transaction_payload, transaction_payload_buf_size, &cose_mac0);

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

#if NEXUS_CHANNEL_LINK_SECURITY_ENABLED
  // returns true if the request should be secured and it was successfully secured, or if
  // no security is required and none is applied. Else returns false
  bool security_ok = true;
  if (nx_secure_request)
  {
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
#else
  if (transaction->message->length > 0) {
#endif
    coap_send_transaction(transaction);

// XXX Currently the `client_cb` will remain valid (unremoved) until a
// response arrives. However, we probably do want to remove it after some
// timeout period (OC_NON_LIFETIME seems fine but too long right now... 10 seconds?)
#if NEXUS_CHANNEL_USE_OC_OBSERVABILITY_AND_CONFIRMABLE_COAP_APIS
    if (client_cb->observe_seq == -1) {
      if (client_cb->qos == LOW_QOS)
        oc_set_delayed_callback(client_cb, &oc_ri_remove_client_cb,
                                OC_NON_LIFETIME);
      else
        oc_set_delayed_callback(client_cb, &oc_ri_remove_client_cb,
                                OC_EXCHANGE_LIFETIME);
    }
#endif // NEXUS_CHANNEL_USE_OC_OBSERVABILITY_AND_CONFIRMABLE_COAP_APIS

    success = true;
  } else {
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
    return false;
  }

  oc_rep_new(transaction->message->data + COAP_MAX_HEADER_SIZE, OC_BLOCK_SIZE);

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

void
oc_free_server_endpoints(oc_endpoint_t *endpoint)
{
  oc_endpoint_t *next;
  while (endpoint != NULL) {
    next = endpoint->next;
    oc_free_endpoint(endpoint);
    endpoint = next;
  }
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
