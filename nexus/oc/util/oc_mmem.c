/*
 * Copyright (c) 2016 Intel Corporation
 *
 * Copyright (c) 2005, Swedish Institute of Computer Science
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
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"

#include "oc_mmem.h"
#include "oc_config.h"
#include "utils/oc_list.h"
#include "port/oc_log.h"
#include <stdint.h>
#include <string.h>
/*
#ifdef OC_MEMORY_TRACE
#include "oc_mem_trace.h"
#include <stdbool.h>
#endif

#ifndef OC_DYNAMIC_ALLOCATION
#if !defined(OC_BYTES_POOL_SIZE) || !defined(OC_INTS_POOL_SIZE) ||             \
  !defined(OC_DOUBLES_POOL_SIZE)
#error "Please define byte, int, double pool sizes in oc_config.h"
#endif // ...POOL_SIZE
*/
#if NEXUS_CHANNEL_OC_SUPPORT_DOUBLES
static double doubles[OC_DOUBLES_POOL_SIZE];
static unsigned int avail_doubles;
#endif
static int64_t ints[OC_INTS_POOL_SIZE];
static unsigned char bytes[OC_BYTES_POOL_SIZE];
static unsigned int avail_bytes, avail_ints;

OC_LIST(bytes_list);
OC_LIST(ints_list);
#if NEXUS_CHANNEL_OC_SUPPORT_DOUBLES
OC_LIST(doubles_list);
#endif
//#else // !OC_DYNAMIC_ALLOCATION
#include <stdlib.h>
//#endif // OC_DYNAMIC_ALLOCATION
/*---------------------------------------------------------------------------*/

size_t
_oc_mmem_alloc(
#ifdef OC_MEMORY_TRACE
  const char *func,
#endif
  struct oc_mmem *m, size_t size, pool pool_type)
{
  if (!m) {
    OC_ERR("oc_mmem is NULL");
    return 0;
  }

  size_t bytes_allocated = 0;
  //OC_DBG("avail_bytes: %ul", avail_bytes);

  switch (pool_type) {
  case BYTE_POOL:
    //OC_DBG("allocating %ul to byte pool", size);
    bytes_allocated += size * sizeof(uint8_t);
#ifdef OC_DYNAMIC_ALLOCATION
    m->ptr = malloc(size);
    m->size = size;
#else  /* OC_DYNAMIC_ALLOCATION */
    if (avail_bytes < size) {
      OC_WRN("byte pool exhausted");
      return 0;
    }
    //OC_DBG("adding allocation to bytes list");
    oc_list_add(bytes_list, m);
    //OC_DBG("added allocation to bytes list");
    m->ptr = &bytes[OC_BYTES_POOL_SIZE - avail_bytes];
    m->size = size;
    avail_bytes -= size;
      //OC_DBG("subtracted bytes");
#endif /* !OC_DYNAMIC_ALLOCATION */
    break;
  case INT_POOL:
    //OC_DBG("allocating %ul to int pool", size);
    bytes_allocated += size * sizeof(int64_t);
#ifdef OC_DYNAMIC_ALLOCATION
    m->ptr = malloc(size * sizeof(int64_t));
    m->size = size;
#else  /* OC_DYNAMIC_ALLOCATION */
    if (avail_ints < size) {
      //OC_WRN("int pool exhausted");
      return 0;
    }
    oc_list_add(ints_list, m);
    m->ptr = &ints[OC_INTS_POOL_SIZE - avail_ints];
    m->size = size;
    avail_ints -= size;
#endif /* !OC_DYNAMIC_ALLOCATION */
    break;
#if NEXUS_CHANNEL_OC_SUPPORT_DOUBLES
  case DOUBLE_POOL:
    //OC_WRN("allocating %ul to double pool!", size);
    bytes_allocated += size * sizeof(double);
#ifdef OC_DYNAMIC_ALLOCATION
    m->ptr = malloc(size * sizeof(double));
    m->size = size;
#else  /* OC_DYNAMIC_ALLOCATION */
    if (avail_doubles < size) {
      //OC_WRN("double pool exhausted");
      return 0;
    }
    oc_list_add(doubles_list, m);
    m->ptr = &doubles[OC_DOUBLES_POOL_SIZE - avail_doubles];
    m->size = size;
    avail_doubles -= size;
#endif /* !OC_DYNAMIC_ALLOCATION */
    break;
#endif
  default:
    break;
  }

#ifdef OC_MEMORY_TRACE
  oc_mem_trace_add_pace(func, bytes_allocated, MEM_TRACE_ALLOC, m->ptr);
#endif

  return (int) bytes_allocated;
}

void
_oc_mmem_free(
#ifdef OC_MEMORY_TRACE
  const char *func,
#endif
  struct oc_mmem *m, pool pool_type)
{
  if (!m) {
    //OC_ERR("oc_mmem is NULL");
    return;
  }

#ifdef OC_MEMORY_TRACE
  unsigned int bytes_freed = m->size;
  switch (pool_type) {
  case INT_POOL:
    bytes_freed *= sizeof(int64_t);
    break;
#if NEXUS_CHANNEL_OC_SUPPORT_DOUBLES
  case DOUBLE_POOL:
    bytes_freed *= sizeof(double);
    break;
#endif
  default:
    break;
  }
  oc_mem_trace_add_pace(func, bytes_freed, MEM_TRACE_FREE, m->ptr);
#endif /* OC_MEMORY_TRACE */

#ifndef OC_DYNAMIC_ALLOCATION
  struct oc_mmem *n;

  if (m->next != NULL) {
    switch (pool_type) {
    case BYTE_POOL:
      memmove(m->ptr, m->next->ptr, &bytes[OC_BYTES_POOL_SIZE - avail_bytes] -
                                      (unsigned char *)m->next->ptr);

      break;
    case INT_POOL:
      memmove(m->ptr, m->next->ptr,
              &ints[OC_INTS_POOL_SIZE - avail_ints] - (int64_t *)m->next->ptr);
      break;
#if NEXUS_CHANNEL_OC_SUPPORT_DOUBLES
    case DOUBLE_POOL:
      memmove(m->ptr, m->next->ptr,
              &doubles[OC_DOUBLES_POOL_SIZE - avail_doubles] -
                (double *)m->next->ptr);
      break;
#endif
    default:
      return;
#pragma GCC diagnostic push
#pragma clang diagnostic ignored "-Wunreachable-code-break"
      break;
#pragma GCC diagnostic pop
    }
    for (n = m->next; n != NULL; n = n->next) {
      n->ptr = (void *)((char *)n->ptr - m->size);
    }
  }

  switch (pool_type) {
  case BYTE_POOL:
    avail_bytes += m->size;
    oc_list_remove(bytes_list, m);
    break;
  case INT_POOL:
    avail_ints += m->size;
    oc_list_remove(ints_list, m);
    break;
#if NEXUS_CHANNEL_OC_SUPPORT_DOUBLES
  case DOUBLE_POOL:
    avail_doubles += m->size;
    oc_list_remove(doubles_list, m);
    break;
#endif
  default:
    // TODO: should not reach here, indicate error
    break;
  }
#else /* !OC_DYNAMIC_ALLOCATION */
  (void)pool_type;
  free(m->ptr);
  m->size = 0;
#endif /* OC_DYNAMIC_ALLOCATION */
}

void
oc_mmem_init(void)
{
#ifndef OC_DYNAMIC_ALLOCATION
  static int inited = 0;
  if (inited) {
    return;
  }
  oc_list_init(bytes_list);
  oc_list_init(ints_list);
#if NEXUS_CHANNEL_OC_SUPPORT_DOUBLES
  oc_list_init(doubles_list);
#endif
  avail_bytes = OC_BYTES_POOL_SIZE;
  avail_ints = OC_INTS_POOL_SIZE;
#if NEXUS_CHANNEL_OC_SUPPORT_DOUBLES
  avail_doubles = OC_DOUBLES_POOL_SIZE;
#endif
  inited = 1;
#endif // OC_DYNAMIC_ALLOCATION
}

#pragma GCC diagnostic pop

#ifdef NEXUS_DEFINED_DURING_TESTING
// used internally in Nexus tests

void
oc_nexus_testing_reinit_mmem_lists(void)
{
  // clear each list. The memory in the lists
  // is not dynamically allocated in Nexus, so
  // resetting the list to a null pointer (reinit)
  // works as well as popping elements from each list.
  // if `OC_DYNAMIC_ALLOCATION` was in use, this approach
  // would result in a memory leak.
  oc_list_init(bytes_list);
  oc_list_init(ints_list);
#if NEXUS_CHANNEL_OC_SUPPORT_DOUBLES
  oc_list_init(doubles_list);
#endif
}
#endif
/*---------------------------------------------------------------------------*/
