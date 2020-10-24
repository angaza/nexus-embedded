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
#pragma GCC diagnostic ignored "-Wshadow"

#include "messaging/coap/engine.h"
#include "oc_signal_event_loop.h"
//#include "port/oc_network_events_mutex.h"
#include "util/oc_memb.h"
#include <stdint.h>
#include <stdio.h>
/*#ifdef OC_DYNAMIC_ALLOCATION
#include <stdlib.h>
#endif // OC_DYNAMIC_ALLOCATION

#ifdef OC_SECURITY
#include "security/oc_tls.h"
#endif // OC_SECURITY
*/
#include "oc_buffer.h"
#include "oc_config.h"
#include "oc_events.h"

OC_PROCESS(message_buffer_handler, "OC Message Buffer Handler");
OC_MEMB(oc_incoming_buffers, oc_message_t, OC_MAX_NUM_CONCURRENT_REQUESTS);
OC_MEMB(oc_outgoing_buffers, oc_message_t, OC_MAX_NUM_CONCURRENT_REQUESTS);

static oc_message_t *
allocate_message(struct oc_memb *pool)
{
  //oc_network_event_handler_mutex_lock();
  oc_message_t *message = (oc_message_t *)oc_memb_alloc(pool);
  //oc_network_event_handler_mutex_unlock();
  if (message) {
/*#ifdef OC_DYNAMIC_ALLOCATION
    message->data = malloc(OC_PDU_SIZE);
    if (!message->data) {
      oc_memb_free(pool, message);
      return NULL;
    }
#endif // OC_DYNAMIC_ALLOCATION
*/
    message->pool = pool;
    message->length = 0;
    message->next = 0;
    message->ref_count = 1;
    message->endpoint.interface_index = -1;
/*#ifdef OC_SECURITY
    message->encrypted = 0;
#endif // OC_SECURITY
#ifndef OC_DYNAMIC_ALLOCATION
    OC_DBG("buffer: Allocated TX/RX buffer; num free: %d",
           oc_memb_numfree(pool));
#endif // !OC_DYNAMIC_ALLOCATION
  }
#ifndef OC_DYNAMIC_ALLOCATION
  else {
    OC_WRN("buffer: No free TX/RX buffers!");
  }
#endif // !OC_DYNAMIC_ALLOCATION
*/
  }
  return message;
}
/*
oc_message_t *
oc_allocate_message_from_pool(struct oc_memb *pool)
{
  if (pool) {
    return allocate_message(pool);
  }
  return NULL;
}

void
oc_set_buffers_avail_cb(oc_memb_buffers_avail_callback_t cb)
{
  oc_memb_set_buffers_avail_cb(&oc_incoming_buffers, cb);
}
*/
oc_message_t *
oc_allocate_message(void)
{
  return allocate_message(&oc_incoming_buffers);
}

oc_message_t *
oc_internal_allocate_outgoing_message(void)
{
  return allocate_message(&oc_outgoing_buffers);
}

void
oc_message_add_ref(oc_message_t *message)
{
  if (message)
    message->ref_count++;
}

void
oc_message_unref(oc_message_t *message)
{
  if (message) {
    message->ref_count--;
    if (message->ref_count <= 0) {
//#ifdef OC_DYNAMIC_ALLOCATION
      //free(message->data);
//#endif // OC_DYNAMIC_ALLOCATION
      struct oc_memb *pool = message->pool;
      oc_memb_free(pool, message);
//#ifndef OC_DYNAMIC_ALLOCATION
      OC_DBG("buffer: freed TX/RX buffer; num free: %d", oc_memb_numfree(pool));
//#endif // !OC_DYNAMIC_ALLOCATION
    }
  }
}

void
oc_recv_message(oc_message_t *message)
{
  if (oc_process_post(&message_buffer_handler, oc_events[INBOUND_NETWORK_EVENT],
                      message) == OC_PROCESS_ERR_FULL)
  {
    OC_WRN("could not post message; unreffing message\n");
    oc_message_unref(message);
  }
  OC_DBG("posted event %#04x to message_buffer_handler process\n", (uint8_t) oc_events[INBOUND_NETWORK_EVENT]);
}

void
oc_send_message(oc_message_t *message)
{
  if (oc_process_post(&message_buffer_handler,
                      oc_events[OUTBOUND_NETWORK_EVENT],
                      message) == OC_PROCESS_ERR_FULL)
    message->ref_count--;

  //_oc_signal_event_loop();
}

/*#ifdef OC_SECURITY
void
oc_close_all_tls_sessions_for_device(size_t device)
{
  oc_process_post(&message_buffer_handler, oc_events[TLS_CLOSE_ALL_SESSIONS],
                  (oc_process_data_t)device);
}

void
oc_close_all_tls_sessions(void)
{
  oc_process_poll(&(oc_tls_handler));
  _oc_signal_event_loop();
}
#endif // OC_SECURITY
*/

OC_PROCESS_THREAD(message_buffer_handler, ev, data)
{
  OC_PROCESS_BEGIN();
  while (1) {
    OC_PROCESS_YIELD();
    OC_DBG("Started buffer handler process with event: %#04x\n", ev);

    if (ev == oc_events[INBOUND_NETWORK_EVENT]) {
      OC_DBG("Inbound network event: decrypted request");
      oc_process_post(&coap_engine, oc_events[INBOUND_RI_EVENT], data);
    } else if (ev == oc_events[OUTBOUND_NETWORK_EVENT]) {
      oc_message_t *message = (oc_message_t *)data;

#ifdef OC_CLIENT
      if (message->endpoint.flags & DISCOVERY) {
        OC_WRN("Unexpected outbound discovery message");
        OC_DBG("Outbound network event: multicast discovery request");
        oc_send_discovery_request(message);
        oc_message_unref(message);
      } else
#endif // OC_CLIENT
/*
#ifdef OC_SECURITY
        if (message->endpoint.flags & SECURED) {
        OC_DBG("Outbound network event: forwarding to TLS");

#ifdef OC_CLIENT
        if (!oc_tls_connected(&message->endpoint)) {
          OC_DBG("Posting INIT_TLS_CONN_EVENT");
          oc_process_post(&oc_tls_handler, oc_events[INIT_TLS_CONN_EVENT],
                          data);
        } else
#endif // OC_CLIENT
        {
          OC_DBG("Posting RI_TO_TLS_EVENT");
          oc_process_post(&oc_tls_handler, oc_events[RI_TO_TLS_EVENT], data);
        }
      } else
#endif // OC_SECURITY*/
      {
// avoid static analysis errors when OC_DBG is a no-op (identical branches).
#ifdef CONFIG_NEXUS_COMMON_OC_DEBUG_LOG_ENABLED
        if (message->endpoint.flags & MULTICAST)
        {
          // Indicative only for now
          OC_DBG("Outbound network event: multicast message");
        }
        else
        {
            OC_DBG("Outbound network event: unicast message");
        }
#endif
        OC_DBG("---------------OC_SEND_BUFFER CALL---------------");
        OC_DBG("Sending %zu byte message to address (scope %i, port %i)", message->length, message->endpoint.addr.ipv6.scope, message->endpoint.addr.ipv6.port);
        OC_LOGbytes(message->endpoint.addr.ipv6.address, 16);
        OC_DBG("Message bytes:");
        OC_LOGbytes(message->data, message->length);
        // Here is a good breakpoint for gdb to examine *message for raw contents
        oc_send_buffer(message);
        oc_message_unref(message);
      }
    }
/*#ifdef OC_SECURITY
    else if (ev == oc_events[TLS_CLOSE_ALL_SESSIONS]) {
      OC_DBG("Signaling to close all TLS sessions from this device");
      oc_process_post(&oc_tls_handler, oc_events[TLS_CLOSE_ALL_SESSIONS], data);
    }
#endif // OC_SECURITY*/
  }
  OC_PROCESS_END();
}

#pragma GCC diagnostic pop
