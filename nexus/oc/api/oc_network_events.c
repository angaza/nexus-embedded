/*
// Copyright (c) 2016-2018 Intel Corporation
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

#include "oc_network_events.h"
#include "oc_buffer.h"
#include "oc_events.h"
#include "oc_signal_event_loop.h"
#include "port/oc_connectivity.h"
#include "utils/oc_list.h"
// hook into Nexus for event processing
#include "include/nxp_common.h"

#ifdef NEXUS_USE_OC_NETWORK_EVENTS_PROCESS
OC_LIST(network_events);
/*#ifdef OC_NETWORK_MONITOR
static bool interface_up, interface_down;
//#endif / OC_NETWORK_MONITOR
*/
static void
oc_process_network_event(void)
{
  //oc_network_event_handler_mutex_lock(); assume one network event at a time for now
  oc_message_t *message = (oc_message_t *)oc_list_pop(network_events);
  while (message != NULL) {
    PRINT("network events message present, processing it\n");
    oc_recv_message(message);
    message = oc_list_pop(network_events);
  }
/*#ifdef OC_NETWORK_MONITOR
  if (interface_up) {
    oc_process_post(&oc_network_events, oc_events[INTERFACE_UP], NULL);
    interface_up = false;
  }
  if (interface_down) {
    oc_process_post(&oc_network_events, oc_events[INTERFACE_DOWN], NULL);
    interface_down = false;
  }
#endif // OC_NETWORK_MONITOR*/
  //oc_network_event_handler_mutex_unlock();
}

OC_PROCESS(oc_network_events, "OC Network Events");
OC_PROCESS_THREAD(oc_network_events, ev, data)
{
  PRINT("OC Network events process starting with event %#04x\n", ev);
  (void)data;
  OC_PROCESS_POLLHANDLER(oc_process_network_event());
  OC_PROCESS_BEGIN();
  while (oc_process_is_running(&(oc_network_events))) {
    OC_PROCESS_YIELD();
/*#ifdef OC_NETWORK_MONITOR
    if (ev == oc_events[INTERFACE_DOWN]) {
      handle_network_interface_event_callback(NETWORK_INTERFACE_DOWN);
    } else if (ev == oc_events[INTERFACE_UP]) {
      handle_network_interface_event_callback(NETWORK_INTERFACE_UP);
    }
#endif // OC_NETWORK_MONITOR*/
  }
  OC_PROCESS_END();
}
#endif // NEXUS_USE_OC_NETWORK_EVENTS_PROCESS

void
oc_network_event(oc_message_t *message)
{
#ifdef NEXUS_USE_OC_NETWORK_EVENTS_PROCESS
  if (!oc_process_is_running(&(oc_network_events))) {
    oc_message_unref(message);
    return;
  }
  //oc_network_event_handler_mutex_lock();
  oc_list_add(network_events, message);
  //oc_network_event_handler_mutex_unlock();

  oc_process_poll(&(oc_network_events));
  //_oc_signal_event_loop();
#else
  // Note: Nexus code which calls `oc_network_event` must call
  // `nxp_common_request_processing` after this returns
  oc_recv_message(message);
#endif // NEXUS_USE_OC_NETWORK_EVENTS_PROCESS
}

/*#ifdef OC_NETWORK_MONITOR
void
oc_network_interface_event(oc_interface_event_t event)
{
  if (!oc_process_is_running(&(oc_network_events))) {
    return;
  }

  oc_network_event_handler_mutex_lock();
  if (event == NETWORK_INTERFACE_DOWN) {
    interface_down = true;
  } else if (event == NETWORK_INTERFACE_UP) {
    interface_up = true;
  } else {
    oc_network_event_handler_mutex_unlock();
    return;
  }
  oc_network_event_handler_mutex_unlock();

  oc_process_poll(&(oc_network_events));
  _oc_signal_event_loop();
}
#endif // OC_NETWORK_MONITOR */
