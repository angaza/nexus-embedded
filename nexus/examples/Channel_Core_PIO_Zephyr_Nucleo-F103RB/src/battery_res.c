/** \file
 * Nexus Channel PAYG Credit OCF Resource (Implementation)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "battery_res.h"
#include "nx_channel.h"
// functions for building a response message
#include "oc/include/oc_ri.h"

// Zephyr API for random number generation
#include <zephyr.h>
// Zephyr logging for easier demonstration purposes
#include <logging/log.h>
LOG_MODULE_REGISTER(battery_res);

#define BATT_CAPACITY_MAH 32000

// used to simulate a connected battery
// For simulation only, real batteries aren't
// modeled this simply
#define SIMULATED_BATTERY_100PCT_MV 14400
#define SIMULATED_BATTERY_80PCT_MV 13250
#define SIMULATED_BATTERY_60PCT_MV 13170
#define SIMULATED_BATTERY_40PCT_MV 13100
#define SIMULATED_BATTERY_20PCT_MV 12900
#define SIMULATED_BATTERY_10PCT_MV 12000
#define SIMULATED_BATTERY_0PCT_MV 10500

/* "Stored" low battery threshold for the
 * battery state.
 * We store this value because its possible for
 * another device to update it via a 'POST' request.
 */
static uint8_t _threshold = 20;

/**
 * GET method for battery resource.
 *
 * Resource Description:
 * This resource indicates the current state of the battery on
 * this device.
 *
 * See also -
 * * https://angaza.github.io/nexus-channel-models/resource_types/core/101-battery/redoc_wrapper.html
 *
 * \param request the request representation.
 * \param interfaces the interface used for this call (not used in basic cases)
 * \param user_data the user data. (not used in basic cases)
 */
static void battery_res_get_handler(oc_request_t* request,
                                    oc_interface_mask_t interfaces,
                                    void* user_data)
{
    // used in certain cases to store data between requests, not
    // usually required.
    (void) user_data;

    // "interfaces" are an Open Connectivity Foundation
    // concept allowing multiple different 'views' into a resource.
    // For example, a GET request using the 'OC_IF_BASELINE' interface
    // would cause the resource type and other metadata to be
    // included in the response message.
    //
    // However, Nexus Channel Core does not require use of these
    // interfaces in most cases, and "OC_IF_RW" or "OC_IF_R"
    // are typically sufficient.
    (void) interfaces;

    LOG_INF("Handling GET request, interface %d\n", interfaces);

    // Now, populate the
    // property strings as defined in spec:
    // https://angaza.github.io/nexus-channel-models/resource_types/core/101-battery/redoc_wrapper.html

    // 'vb' (battery voltage in mV) and 'cp' (charge percentage) are the only
    // *required* properties.
    // Others are optional, and included in this demo for reference.

    // Responses are 'built' using these helper functions,
    // where each property is explicitly set.
    oc_rep_begin_root_object();

    // In this demo, we use a random value to populate
    // the values shown in the response.
    // In a real implementation, product-specific
    // functions to get the battery values would be used instead.
    uint32_t random_value = sys_rand32_get();

    // set battery mV and charge percentage to random values
    // note: (charge pct is not easily related directly to mV
    // on real LiFePo4 batteries)
    uint8_t charge_pct = (random_value % 100);
    uint32_t battery_mv;
    if (charge_pct > 80)
    {
        battery_mv = SIMULATED_BATTERY_100PCT_MV;
    }
    else if (charge_pct > 60)
    {
        battery_mv = SIMULATED_BATTERY_80PCT_MV;
    }
    else if (charge_pct > 40)
    {
        battery_mv = SIMULATED_BATTERY_60PCT_MV;
    }
    else if (charge_pct > 20)
    {
        battery_mv = SIMULATED_BATTERY_40PCT_MV;
    }
    else if (charge_pct > 10)
    {
        battery_mv = SIMULATED_BATTERY_20PCT_MV;
    }
    else if (charge_pct > 5)
    {
        battery_mv = SIMULATED_BATTERY_10PCT_MV;
    }
    else
    {
        battery_mv = SIMULATED_BATTERY_0PCT_MV;
    }

    // ----- Required parameters ----- //
    // battery voltage in mV
    oc_rep_set_int(root, vb, battery_mv);
    // charge percentage 0-100%
    oc_rep_set_int(root, cp, charge_pct);

    // ----- Optional parameters ----- //
    // threshold for low battery warning 0-100%
    oc_rep_set_int(root, th, _threshold);
    // battery capacity in mAh
    oc_rep_set_int(root, ca, BATT_CAPACITY_MAH);
    // battery discharging (t/f)
    oc_rep_set_boolean(root, ds, random_value & 0x01);
    // battery charging (t/f)
    oc_rep_set_boolean(root, cg, random_value & 0x02);
    // low battery warning
    if (charge_pct < _threshold)
    {
        oc_rep_set_boolean(root, lb, true);
    }
    else
    {
        oc_rep_set_boolean(root, lb, false);
    }
    // battery fault detected (t/f)
    oc_rep_set_boolean(root, ft, false);
    // seconds since sampled (0, since we did not use 'cached' values)
    oc_rep_set_int(root, ss, 0);

    // mark the response payload 'finished'
    oc_rep_end_root_object();

    // respond with code "CONTENT 2.05"
    oc_send_response(request, OC_STATUS_OK);
}

/**
 * POST method for battery resource.
 *
 * Requires a threshold value ('th') which will update
 * the low battery warning threshold level.
 *
 * \param request the request representation.
 * \param interfaces the used interfaces during the request.
 * \param user_data the supplied user data.
 */

static void battery_res_post_handler(oc_request_t* request,
                                     oc_interface_mask_t interfaces,
                                     void* user_data)
{
    (void) interfaces;
    (void) user_data;

    LOG_INF("Handling POST request, interface %d\n", interfaces);

    // The 'rep' is a way to walk through the request payload.
    // In this case, there is only one node expected ('th'),
    // but in some POST cases, there may be multiple data elements.

    const oc_rep_t* rep = request->request_payload;

    // error if the 'th' parameter is not present,
    // or if any other parameters besides 'th' are present.
    bool error_state = true;
    uint8_t new_threshold = 0;

    while (rep != NULL)
    {
        if (strncmp(oc_string(rep->name), "th", 2) != 0)
        {
            LOG_WRN("Received unexpected property in POST body");
        }
        else if (rep->type != OC_REP_INT)
        {
            LOG_WRN("`th` received, but is not an integer");
        }
        else
        {
            error_state = false;
            new_threshold = rep->value.integer;
            LOG_INF("Received %d as new threshold value", new_threshold);
        }
        rep = rep->next;
    }

    // if the request was properly formatted, update threshold
    // and respond with the newly updated threshold.
    if (!error_state)
    {
        LOG_INF("Setting **%d** as new low battery threshold value.",
                new_threshold);
        _threshold = new_threshold;

        oc_rep_begin_root_object();
        oc_rep_set_int(root, th, _threshold);
        oc_rep_end_root_object();

        LOG_INF("Responding with 204 to POST");
        // respond with code "CHANGED 2.04"
        oc_send_response(request, OC_STATUS_CHANGED);
    }
    else
    {
        LOG_WRN("Responding with 400 to POST");
        // respond with code "BAD REQUEST 4.00"
        oc_send_response(request, OC_STATUS_BAD_REQUEST);
    }
}

/* Initialize the state of the battery resource.
 * In a real implementation, we would read data from
 * an actual battery. Here, we use arbitrary values or
 * read from MCU onboard data (e.g. VDD rail).
 *
 * We also 'register' the resource handlers for GET and POST
 * here, so that incoming requests for either of these
 * methods will processed by the appropriate function.
 */
void battery_res_init(void)
{
    LOG_INF("Initializing battery resource\n");
    const oc_interface_mask_t if_mask_arr[] = {OC_IF_RW};

    // register both GET and POST as 'unsecured' methods.
    // ('secured' methods are optional, and will use Nexus Channel
    // link security to authorize requests to a
    // given resources method (e.g. battery POST) depending
    // on whether the requesting device is securely linked
    // to this one or not.
    // [Nexus Channel 'Core' does not implement or use link security]
    const struct nx_channel_resource_props batt_res_props = {
        // what URI should this resource be available at?
        .uri = "/batt",
        // 'resource_type' string is optional (and often unused)
        .resource_type = "",
        // 'rtr' is 101 for battery, as specified in the resource type
        // definition
        // Specifying this field will allow other devices to find out what
        // resource 'types' this device has available via discovery (not yet
        // implemented)
        .rtr = 101,
        // there is 1 interface supported ("RW"), this is typical.
        .num_interfaces = 1,
        .if_masks = if_mask_arr,
        // name of method to handle incoming GET requests for this resource
        .get_handler = battery_res_get_handler,
        // [app-level link security not enabled in Nexus Channel Core]
        .get_secured = false,
        // name of method to handle incoming POST requests for this resource
        .post_handler = battery_res_post_handler,
        // [app-level link security not enabled in Nexus Channel Core]
        .post_secured = false};

    if (nx_channel_register_resource(&batt_res_props))
    {
        LOG_ERR("Failed to initialize battery resource!\n");
        // should not reach here
    }

    LOG_INF("Successfully registered battery resource\n");
}