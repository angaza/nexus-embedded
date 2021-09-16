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

#include <stdint.h>
#include <stdio.h>

#include "port/oc_assert.h"
#include "port/oc_clock.h"
#include "port/oc_connectivity.h"

#include "util/oc_etimer.h"
#include "util/oc_process.h"

#include "oc_api.h"
#include "oc_core_res.h"

#include "oc_main.h"

static bool initialized = false;
static const oc_handler_t *app_callbacks;

static void
oc_shutdown_all_devices(void)
{
  //size_t device;
  //for (device = 0; device < oc_core_get_num_devices(); device++) {
    //oc_connectivity_shutdown(device);
  //}

  //oc_network_event_handler_mutex_destroy();
  oc_core_shutdown();
}

int
oc_main_init(const oc_handler_t *handler)
{
  int ret;

  if (initialized == true)
    return 0;

  app_callbacks = handler;

/*#ifdef OC_MEMORY_TRACE
  oc_mem_trace_init();
#endif // OC_MEMORY_TRACE
*/

  oc_ri_init();
  oc_core_init();
  //oc_network_event_handler_mutex_init();

  ret = app_callbacks->init();
  if (ret < 0) {
    oc_ri_shutdown();
    oc_shutdown_all_devices();
    goto ERR;
  }

  if (app_callbacks->register_resources)
    app_callbacks->register_resources();
  OC_DBG("oc_main: stack initialized");

  initialized = true;

/*#ifdef OC_CLIENT
  if (app_callbacks->requests_entry)
    app_callbacks->requests_entry();
#endif*/

  return 0;

ERR:
  OC_ERR("oc_main: error in stack initialization");
  return ret;
}

oc_clock_time_t
oc_main_poll(void)
{
  oc_clock_time_t next_event_poll_second = oc_etimer_request_poll();
  OC_DBG("oc_main_poll: oc_etimer next event poll second = %d\n", (int) next_event_poll_second);
  while (oc_process_run()) {
    next_event_poll_second = oc_etimer_request_poll();
  }
  return next_event_poll_second;
}

void
oc_main_shutdown(void)
{
  if (initialized == false)
    return;

  initialized = false;
  oc_ri_shutdown();
  oc_shutdown_all_devices();

  app_callbacks = NULL;

/*#ifdef OC_MEMORY_TRACE
  oc_mem_trace_shutdown();
#endif // OC_MEMORY_TRACE
*/
}

bool
oc_main_initialized(void)
{
  return initialized;
}
