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
/**
  @file
*/
#ifndef OC_CONNECTIVITY_H
#define OC_CONNECTIVITY_H

#include "messaging/coap/conf.h"
#include "port/oc_config.h"
#include "oc_endpoint.h"
#include "oc_network_events.h"
//#include "oc_session_events.h"
#include "port/oc_log.h"
#include "util/oc_process.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct oc_message_s
{
  struct oc_message_s *next;
  struct oc_memb *pool;
  oc_endpoint_t endpoint;
  size_t length;
  uint8_t ref_count;
  uint8_t data[NEXUS_CHANNEL_MAX_COAP_TOTAL_MESSAGE_SIZE];
};


int oc_send_buffer(oc_message_t *message);
/*
int oc_connectivity_init(size_t device);

void oc_connectivity_shutdown(size_t device);
*/
void oc_send_discovery_request(oc_message_t *message);

#ifdef __cplusplus
}
#endif

#endif /* OC_CONNECTIVITY_H */
