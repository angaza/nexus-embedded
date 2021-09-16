/*
// Copyright (c) 2017 Intel Corporation
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
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"

#include "oc_endpoint.h"
#include "oc_core_res.h"
#include "port/oc_connectivity.h"
//#include "port/oc_network_events_mutex.h"
#include "util/oc_memb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OC_SCHEME_COAP "coap://"
#define OC_SCHEME_COAPS "coaps://"
#define OC_SCHEME_COAP_TCP "coap+tcp://"
#define OC_SCHEME_COAPS_TCP "coaps+tcp://"

#define OC_IPV6_ADDRSTRLEN (46)
#define OC_IPV4_ADDRSTRLEN (16)
#define OC_IPV6_ADDRLEN (16)
#define OC_IPV4_ADDRLEN (4)

void
oc_endpoint_set_di(oc_endpoint_t *endpoint, oc_uuid_t *di)
{
  if (endpoint && di) {
    memcpy(endpoint->di.id, di->id, 16);
  }
}

int
oc_ipv6_endpoint_is_link_local(oc_endpoint_t *endpoint)
{
  if (!endpoint || !(endpoint->flags & IPV6)) {
    return -1;
  }
  if (endpoint->addr.ipv6.address[0] == 0xfe &&
      endpoint->addr.ipv6.address[1] == 0x80) {
    return 0;
  }
  return -1;
}

int
oc_endpoint_compare_address(oc_endpoint_t *ep1, oc_endpoint_t *ep2)
{
  if (!ep1 || !ep2)
    return -1;

  if ((ep1->flags & ep2->flags) & IPV6) {
    if (memcmp(ep1->addr.ipv6.address, ep2->addr.ipv6.address, 16) == 0) {
      return 0;
    }
    return -1;
  }
#ifdef OC_IPV4
  else if ((ep1->flags & ep2->flags) & IPV4) {
    if (memcmp(ep1->addr.ipv4.address, ep2->addr.ipv4.address, 4) == 0) {
      return 0;
    }
    return -1;
  }
#endif /* OC_IPV4 */
  // TODO: Add support for other endpoint types
  return -1;
}

int
oc_endpoint_compare(const oc_endpoint_t *ep1, const oc_endpoint_t *ep2)
{
  if (!ep1 || !ep2)
    return -1;

  if ((ep1->flags & ~MULTICAST) != (ep2->flags & ~MULTICAST) ||
      ep1->device != ep2->device) {
    return -1;
  }
  if (ep1->flags & IPV6) {
    if (memcmp(ep1->addr.ipv6.address, ep2->addr.ipv6.address, 16) == 0 &&
        ep1->addr.ipv6.port == ep2->addr.ipv6.port) {
      return 0;
    }
    return -1;
  }
#ifdef OC_IPV4
  else if (ep1->flags & IPV4) {
    if (memcmp(ep1->addr.ipv4.address, ep2->addr.ipv4.address, 4) == 0 &&
        ep1->addr.ipv4.port == ep2->addr.ipv4.port) {
      return 0;
    }
    return -1;
  }
#endif /* OC_IPV4 */
  // TODO: Add support for other endpoint types
  return -1;
}

void
oc_endpoint_copy(oc_endpoint_t *dst, oc_endpoint_t *src)
{
  if (dst && src) {
    memcpy(dst, src, sizeof(oc_endpoint_t));
    dst->next = NULL;
  }
}

#pragma GCC diagnostic pop
