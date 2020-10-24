/*
 *
 *
 *
 * Note: Portions of this file were automatically generated using the
 * IoTivity "DeviceBuilder", a project which performs automatic generation
 * of OCF resources from OneIOTa specification files.
 * The input specification file used is: https://www.oneiota.org/revisions/5666
 * For more info: https://github.com/openconnectivityfoundation/DeviceBuilder
 */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include "nexus_batt_resource.h"
#include <stdio.h> // for snprintf

// time included for timestamp when data is updated
#include <time.h>

// Nexus Channel/OCF related includes
#include "include/nx_channel.h"
#include "oc/include/oc_api.h"
#include "oc/include/oc_rep.h"
#include "oc/port/oc_log.h"

// includes for simulated responses for demonstration
#include "include/nx_common.h"
#include "oc/include/oc_buffer.h"
#include "oc/include/oc_endpoint.h"
#include "oc/messaging/coap/coap.h"

void nx_network_receive(const void* const bytes_received,
                        uint32_t bytes_count,
                        const struct nx_id* const source);

#define btoa(x) ((x) ? "true" : "false")

#define MAX_STRING 30 /* max size of the strings. */
#define MAX_PAYLOAD_STRING 65 /* max size strings in the payload */
#define MAX_ARRAY 10 /* max size of the array */

// "global" properties for simulated messages
static uint16_t mid = 123;

// "global" properties for the battery resource. These represent
// the current state of the battery on this device.

/* global property variables for path: "/batt" */
static char g_batt_RESOURCE_PROPERTY_NAME_batterythreshold[] =
    "batterythreshold"; /* the name for the attribute */
int g_batt_batterythreshold =
    20; /* current value of property "batterythreshold" The threshold
           percentage for the low battery warning. */
static char g_batt_RESOURCE_PROPERTY_NAME_capacity[] =
    "capacity"; /* the name for the attribute */

// Modified by Angaza to uint32_t, instead of double. //
uint32_t g_batt_capacity = 3000; /* current value of property "capacity"  The
                                    total capacity in Amp-hours (Ah). */

static char g_batt_RESOURCE_PROPERTY_NAME_charge[] =
    "charge"; /* the name for the attribute */
int g_batt_charge =
    50; /* current value of property "charge" The current charge percentage. */
static char g_batt_RESOURCE_PROPERTY_NAME_charging[] =
    "charging"; /* the name for the attribute */
bool g_batt_charging =
    false; /* current value of property "charging" The status of charging. */
static char g_batt_RESOURCE_PROPERTY_NAME_defect[] =
    "defect"; /* the name for the attribute */
bool g_batt_defect = false; /* current value of property "defect" Battery defect
                               detected. True = defect, False = no defect */
static char g_batt_RESOURCE_PROPERTY_NAME_discharging[] =
    "discharging"; /* the name for the attribute */
bool g_batt_discharging = false; /* current value of property "discharging" The
                                    status of discharging. */
static char g_batt_RESOURCE_PROPERTY_NAME_lowbattery[] =
    "lowbattery"; /* the name for the attribute */
bool g_batt_lowbattery =
    false; /* current value of property "lowbattery" The status of the low
              battery warning based upon the defined threshold. */
static char g_batt_RESOURCE_PROPERTY_NAME_timestamp[] =
    "timestamp"; /* the name for the attribute */
char g_batt_timestamp[MAX_PAYLOAD_STRING] =
    "2015-11-05T14:30:00.20Z"; /* current value of property "timestamp" An
                                  RFC3339 formatted time indicating when the
                                  data was observed (e.g.: 2016-02-15T09:19Z,
                                  1996-12-19T16:39:57-08:00). Note that 1/100
                                  time resolution should be used. */

//
// FORWARD DECLARATIONS //
//
// 'initialize_variables' initializes a set of variables which represent
// the current 'state' of the battery (charge level, thresholds, etc).
void initialize_variables(void);

// This function is called to handle a GET request for the resource
static void get_batt(oc_request_t* request,
                     oc_interface_mask_t interfaces,
                     void* user_data);
// Called to handle a POST request for the resource
static void post_batt(oc_request_t* request,
                      oc_interface_mask_t interfaces,
                      void* user_data);

//
// Example code integrating with Nexus Channel (init)
//
void battery_resource_init(void)
{
    // reinitialize simulation parameters
    mid = 123;

    // initialize the variable values exposed by this resource. In a real
    // implementation, this would set these to initial readings taken for
    // each property (battery charge remaining, etc).
    initialize_variables();

    // Interfaces define 'how' a resource may be interacted with. All
    // resources must implement 'baseline', and most resources will implement
    // either 'rw' (for read-write) in addition.
    const oc_interface_mask_t if_mask_arr[] = {OC_IF_RW, OC_IF_BASELINE};

    // This makes the battery resource 'known' to Nexus Channel, and
    // registers the GET endpoint as 'unsecured'.
    // The resource is registered at URI '/batt', and the type is
    // "oic.r.energy.battery" (since this is a standard OIC/OCF resource).
    // (Note that this implementation is more complex than the
    // nexus.channel.core.battery model here:
    // https://angaza.github.io/nexus-channel-models/resource_types/core/101-battery/redoc_wrapper.html)
    const struct nx_channel_resource_props batt_props = {
        .uri = "/batt",
        .resource_type = "oic.r.energy.battery",
        .rtr = 65005,
        .num_interfaces = 2,
        .if_masks = if_mask_arr,
        .get_handler = get_batt,
        .get_secured = false,
        .post_handler = post_batt,
        .post_secured = false};

    nx_channel_error result = nx_channel_register_resource(&batt_props);

    if (result != NX_CHANNEL_ERROR_NONE)
    {
        // debug only - should not occur
        OC_WRN("Error registering battery resource");
    }

    // at this point, any incoming messages received by Nexus Channel for
    // this endpoint will be properly handled
}

void battery_resource_print_status(void)
{
    printf("\nBattery Charge: %u\n", g_batt_charge);
    printf("Low battery Threshold: %u\n", g_batt_batterythreshold);
    printf("Low battery warning active? %u\n", g_batt_lowbattery);
}

void battery_resource_simulate_get(void)
{
    // simulated GET request to battery endpoint
    coap_packet_t request_packet = {0};
    coap_udp_init_message(&request_packet, COAP_TYPE_NON, COAP_GET, mid);
    mid++; // increment message ID to prevent duplicate requests
    coap_set_header_uri_path(&request_packet, "/batt", strlen("/batt"));
    coap_set_header_content_format(&request_packet, APPLICATION_VND_OCF_CBOR);
    const uint8_t token[1] = {0x01}; // dummy token
    coap_set_token(&request_packet, token, sizeof(token));

    uint8_t send_buffer[200] = {0};
    uint32_t send_length = coap_serialize_message(&request_packet, send_buffer);
    const struct nx_id simulated_client_nx_id = {0, 0xAFBB440D};
    nx_channel_network_receive(
        send_buffer, send_length, &simulated_client_nx_id);
}

void battery_resource_simulate_post_update_properties(uint8_t battery_threshold)
{
    // now, simulate a POST request to the battery endpoint and update it
    coap_packet_t request_packet = {0};
    coap_udp_init_message(&request_packet, COAP_TYPE_NON, COAP_POST, mid);
    mid++; // increment message ID to prevent duplicate requests
    coap_set_header_uri_path(&request_packet, "/batt", strlen("/batt"));
    coap_set_header_content_format(&request_packet, APPLICATION_VND_OCF_CBOR);
    const uint8_t token[1] = {0x01}; // dummy token
    coap_set_token(&request_packet, token, sizeof(token));

    // to simplify formatting, we allow thresholds between 0 and 20%.
    // This keeps a constant CBOR size (vs one additional byte)
    uint8_t request_data_cbor[19] = {
        0xA1, 0x70, 0x62, 0x61, 0x74, 0x74, 0x65, 0x72, 0x79,
        0x74, 0x68, 0x72, 0x65, 0x73, 0x68, 0x6F, 0x6C, 0x64,
        0x00, /// will be modified
    };
    if (battery_threshold > 20)
    {
        return; // no-op
    }
    request_data_cbor[18] = battery_threshold;

    coap_set_payload(
        &request_packet, request_data_cbor, sizeof(request_data_cbor));

    uint8_t send_buffer[200] = {0};
    uint32_t send_length = coap_serialize_message(&request_packet, send_buffer);
    const struct nx_id simulated_client_nx_id = {0, 0xAFBB440D};

    nx_channel_network_receive(
        send_buffer, send_length, &simulated_client_nx_id);
}

void _battery_resource_update_timestamp(void)
{
    // get current system time from clock
    static time_t cur_time;
    time(&cur_time);

    // Convert into GMT/UTC
    struct tm* tm_info;
    tm_info = gmtime(&cur_time);

    // Format into RFC3339 string representation for OCF resource
    // Note: This is performed to conform strictly to the OCF 'battery'
    // resource, it is possible to include a separate, 'seconds' time field
    // in a custom resource if desired (or, within this resource), if the
    // application does not need *strictly* compliant standard OCF resource
    // models.
    // YYYY-MM-DDTHH:MM:SSZ
    snprintf(&g_batt_timestamp[0],
             MAX_PAYLOAD_STRING,
             "%4d-%02d-%02dT%02d:%02d:%02dZ",
             (tm_info->tm_year + 1900),
             tm_info->tm_mon + 1,
             tm_info->tm_mday,
             tm_info->tm_hour,
             tm_info->tm_min,
             tm_info->tm_sec);
}

void _battery_resource_update_low_batt_alarm(void)
{
    if (g_batt_charge < g_batt_batterythreshold)
    {
        g_batt_lowbattery = true;
    }
    else
    {
        g_batt_lowbattery = false;
    }
}

void battery_resource_update_charge(uint8_t charge_percent)
{
    g_batt_charge = charge_percent;

    _battery_resource_update_low_batt_alarm();
}

void battery_resource_update_low_threshold(uint8_t threshold_percent)
{
    g_batt_batterythreshold = threshold_percent;

    _battery_resource_update_low_batt_alarm();
}

/*
 * All code below this line is generated using OCF DeviceBuilder, and is
 * under the Apache license terms which follow:
 -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 Copyright 2017-2019 Open Connectivity Foundation
-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
*/

/**
 * intializes the global variables
 * registers and starts the handler
 */
void initialize_variables(void)
{
    /* initialize global variables for resource "/batt" */
    g_batt_batterythreshold =
        20; /* current value of property "batterythreshold" The threshold
               percentage for the low battery warning. */
    g_batt_capacity = 3000; /* current value of property "capacity"  The total
                               capacity in Amp-hours (Ah). */
    g_batt_charge = 50; /* current value of property "charge" The current charge
                           percentage. */
    g_batt_charging =
        false; /* current value of property "charging" The status of charging.
                */
    g_batt_defect = false; /* current value of property "defect" Battery defect
                              detected. True = defect, False = no defect */
    g_batt_discharging = false; /* current value of property "discharging" The
                                   status of discharging. */
    g_batt_lowbattery = false; /* current value of property "lowbattery" The
                                  status of the low battery warning based upon
                                  the defined threshold. */
    strcpy(
        g_batt_timestamp,
        "2015-11-05T14:30:00.20Z"); /* current value of property "timestamp" An
                                       RFC3339 formatted time indicating when
                                       the data was observed (e.g.:
                                       2016-02-15T09:19Z,
                                       1996-12-19T16:39:57-08:00). Note that
                                       1/100 time resolution should be used. */
}

/**
 * helper function to check if the POST input document contains
 * the common readOnly properties or the resouce readOnly properties
 * @param name the name of the property
 * @return the error_status, e.g. if error_status is true, then the input
 * document contains something illegal
 */
static bool check_on_readonly_common_resource_properties(oc_string_t name,
                                                         bool error_state)
{
    if (strcmp(oc_string(name), "n") == 0)
    {
        error_state = true;
        PRINT("   property \"n\" is ReadOnly \n");
    }
    else if (strcmp(oc_string(name), "if") == 0)
    {
        error_state = true;
        PRINT("   property \"if\" is ReadOnly \n");
    }
    else if (strcmp(oc_string(name), "rt") == 0)
    {
        error_state = true;
        PRINT("   property \"rt\" is ReadOnly \n");
    }
    else if (strcmp(oc_string(name), "id") == 0)
    {
        error_state = true;
        PRINT("   property \"id\" is ReadOnly \n");
    }
    else if (strcmp(oc_string(name), "id") == 0)
    {
        error_state = true;
        PRINT("   property \"id\" is ReadOnly \n");
    }
    return error_state;
}

/**
 * post method for "/batt" resource.
 * The function has as input the request body, which are the input values of the
 * POST method.
 * The input values (as a set) are checked if all supplied values are correct.
 * If the input values are correct, they will be assigned to the global property
 * values.
 * Resource Description:
 * Sets current battery values
 *
 * @param request the request representation.
 * @param interfaces the used interfaces during the request.
 * @param user_data the supplied user data.
 */
static void post_batt(oc_request_t* request,
                      oc_interface_mask_t interfaces,
                      void* user_data)
{
    (void) interfaces;
    (void) user_data;
    bool error_state = false;
    PRINT("-- Begin post_batt:\n");
    oc_rep_t* rep = request->request_payload;

    /* loop over the request document for each required input field to check if
     * all required input fields are present */
    bool var_in_request = false;
    rep = request->request_payload;
    while (rep != NULL)
    {
        if (strcmp(oc_string(rep->name),
                   g_batt_RESOURCE_PROPERTY_NAME_batterythreshold) == 0)
        {
            var_in_request = true;
        }
        rep = rep->next;
    }
    if (var_in_request == false)
    {
        error_state = true;
        PRINT(" required property: 'batterythreshold' not in request\n");
    }
    /* loop over the request document to check if all inputs are ok */
    rep = request->request_payload;
    while (rep != NULL)
    {
        PRINT("key: (check) %s \n", oc_string(rep->name));

        error_state = check_on_readonly_common_resource_properties(rep->name,
                                                                   error_state);
        if (strcmp(oc_string(rep->name),
                   g_batt_RESOURCE_PROPERTY_NAME_batterythreshold) == 0)
        {
            /* property "batterythreshold" of type integer exist in payload */
            if (rep->type != OC_REP_INT)
            {
                error_state = true;
                PRINT("   property 'batterythreshold' is not of type int %d \n",
                      rep->type);
            }

            int value_max = (int) rep->value.integer;
            if (value_max > 100)
            {
                /* check the maximum range */
                PRINT("   property 'batterythreshold' value exceed max : 0 >  "
                      "value: %d \n",
                      value_max);
                error_state = true;
            }
        }
        rep = rep->next;
    }
    /* if the input is ok, then process the input document and assign the global
     * variables */
    if (error_state == false)
    {
        /* loop over all the properties in the input document */
        oc_rep_t* rep = request->request_payload;
        while (rep != NULL)
        {
            PRINT("key: (assign) %s \n", oc_string(rep->name));
            /* no error: assign the variables */

            if (strcmp(oc_string(rep->name),
                       g_batt_RESOURCE_PROPERTY_NAME_batterythreshold) == 0)
            {
                /* assign "batterythreshold" */
                PRINT("  property 'batterythreshold' : %d\n",
                      (int) rep->value.integer);
                g_batt_batterythreshold = (int) rep->value.integer;
            }
            rep = rep->next;
        }
        /* set the response */
        PRINT("Set response \n");
        oc_rep_begin_root_object(); // changed to 'begin', Angaza
        /*oc_process_baseline_interface(request->resource); */
        oc_rep_set_int(root, batterythreshold, g_batt_batterythreshold);
        oc_rep_set_int(root, capacity, g_batt_capacity);
        oc_rep_set_int(root, charge, g_batt_charge);
        oc_rep_set_boolean(root, charging, g_batt_charging);
        oc_rep_set_boolean(root, defect, g_batt_defect);
        oc_rep_set_boolean(root, discharging, g_batt_discharging);
        oc_rep_set_boolean(root, lowbattery, g_batt_lowbattery);
        oc_rep_set_text_string(root, timestamp, g_batt_timestamp);

        oc_rep_end_root_object();
        /* TODO: ACTUATOR add here the code to talk to the HW if one implements
           an actuator.
           one can use the global variables as input to those calls
           the global values have been updated already with the data from the
           request */
        oc_send_response(request, OC_STATUS_CHANGED);
    }
    else
    {
        PRINT("  Returning Error \n");
        /* TODO: add error response, if any */
        // oc_send_response(request, OC_STATUS_NOT_MODIFIED);
        oc_send_response(request, OC_STATUS_BAD_REQUEST);
    }
    PRINT("-- End post_batt\n");
}

/**
 * get method for "/batt" resource.
 * function is called to intialize the return values of the GET method.
 * initialisation of the returned values are done from the global property
 * values.
 * Resource Description:
 * This Resource describes the attributes associated with a battery. The
 * Property "charge" is an integer showing the current battery charge level as a
 * percentage in the range 0 (fully discharged) to 100 (fully charged). The
 * Property "capacity" represents the total capacity of battery in Amp Hours
 * (Ah). The "charging" status and "discharging" status are represented by
 * boolean values set to "true" indicating enabled and "false" indicating
 * disabled. Low battery status is represented by a boolean value set to "true"
 * indicating low charge level and "false" indicating otherwise, based upon the
 * battery threshold represented as a percentage.
 *
 * @param request the request representation.
 * @param interfaces the interface used for this call
 * @param user_data the user data.
 */
static void
get_batt(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
    // update timestamp
    _battery_resource_update_timestamp();
    (void) user_data; /* variable not used */
    /* TODO: SENSOR add here the code to talk to the HW if one implements a
       sensor.
       the call to the HW needs to fill in the global variable before it returns
       to this function here.
       alternative is to have a callback from the hardware that sets the global
       variables.

       The implementation always return everything that belongs to the resource.
       this implementation is not optimal, but is functionally correct and will
       pass CTT1.2.2 */
    bool error_state = false;

    PRINT("-- Begin get_batt: interface %d\n", interfaces);
    oc_rep_begin_root_object(); // changed to 'begin', Angaza
    switch (interfaces)
    {
        case OC_IF_BASELINE:
        /* fall through */
        case OC_IF_RW:
            PRINT("\tadding baseline info\n");
            oc_process_baseline_interface(request->resource);

            /* property (integer) 'batterythreshold' */
            oc_rep_set_int(root, batterythreshold, g_batt_batterythreshold);
            PRINT("\t%s:\t%d\n",
                  g_batt_RESOURCE_PROPERTY_NAME_batterythreshold,
                  g_batt_batterythreshold);
            /* property (integer) 'capacity' */
            // Modified by Angaza, replacing double with int //
            oc_rep_set_int(root, capacity, g_batt_capacity);
            // Modified by Angaza to print int, instead of double. //
            PRINT("\t%s:\t\t%d\n",
                  g_batt_RESOURCE_PROPERTY_NAME_capacity,
                  g_batt_capacity);
            /* property (integer) 'charge' */
            oc_rep_set_int(root, charge, g_batt_charge);
            PRINT("\t%s:\t\t\t%d\n",
                  g_batt_RESOURCE_PROPERTY_NAME_charge,
                  g_batt_charge);
            /* property (boolean) 'charging' */
            oc_rep_set_boolean(root, charging, g_batt_charging);
            PRINT("\t%s:\t\t%s\n",
                  g_batt_RESOURCE_PROPERTY_NAME_charging,
                  btoa(g_batt_charging));
            /* property (boolean) 'defect' */
            oc_rep_set_boolean(root, defect, g_batt_defect);
            PRINT("\t%s:\t\t\t%s\n",
                  g_batt_RESOURCE_PROPERTY_NAME_defect,
                  btoa(g_batt_defect));
            /* property (boolean) 'discharging' */
            oc_rep_set_boolean(root, discharging, g_batt_discharging);
            PRINT("\t%s:\t\t%s\n",
                  g_batt_RESOURCE_PROPERTY_NAME_discharging,
                  btoa(g_batt_discharging));
            /* property (boolean) 'lowbattery' */
            oc_rep_set_boolean(root, lowbattery, g_batt_lowbattery);
            PRINT("\t%s:\t\t%s\n",
                  g_batt_RESOURCE_PROPERTY_NAME_lowbattery,
                  btoa(g_batt_lowbattery));
            /* property (string) 'timestamp' */
            oc_rep_set_text_string(root, timestamp, g_batt_timestamp);
            PRINT("\t%s:\t\t%s\n",
                  g_batt_RESOURCE_PROPERTY_NAME_timestamp,
                  g_batt_timestamp);
            break;
        default:
            break;
    }
    oc_rep_end_root_object();
    if (error_state == false)
    {
        oc_send_response(request, OC_STATUS_OK);
    }
    else
    {
        oc_send_response(request, OC_STATUS_BAD_OPTION);
    }
    PRINT("-- End get_batt\n");
}

#pragma GCC diagnostic pop
