/** \file
 * Nexus Channel Common Module (Implementation)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "src/nexus_channel_core.h"

// for `NEXUS_COMMON_IDLE_TIME_BETWEEN_PROCESS_CALL_SECONDS`
#include "src/nexus_common_internal.h"

#if NEXUS_CHANNEL_CORE_ENABLED

    #include "oc/include/oc_api.h"
    #include "oc/messaging/coap/engine.h"
    #include "src/nexus_channel_res_link_hs.h"
    #include "src/nexus_channel_res_lm.h"
    #include "src/nexus_channel_res_payg_credit.h"
    #include "src/nexus_channel_sm.h"
    #include "src/nexus_oc_wrapper.h"
    #include <string.h>

/** Passed as part of `oc_handler_t` struct in `nexus_channel_core_init`
 * to initialize internal OCF platform/device models.
 */
static int _nexus_channel_core_internal_init(void)
{
    // initialize platform; no additional initialization or context required
    int ret = oc_init_platform("Angaza", NULL, NULL);
    ret |= oc_add_device("/oic/d/",
                         "angaza.io.nexus",
                         "Nexus Channel",
                         "ocf.2.1.1",
                         "ocf.res.1.3.0", // for legacy device support
                         NULL,
                         NULL);

    return ret;
}

bool nexus_channel_core_init(void)
{
    // Initialize CoaP
    coap_init_engine();

    /** Setup functions before calling `oc_main_init`; see docstring of
     * `oc_main_init` in `oc_api.h`.
     */
    oc_set_con_res_announced(false); // do not expose device config resource
    // oc_set_factory_presets_cb; // required to set manufacturer certificates
    // oc_set_max_app_data_size // only required with dynamic allocation
    // oc_set_random_pin_callback // only required with random PIN onboarding
    // oc_storage_config // only required with random PIN onboarding

    /** Initialize IoTivity-lite stack:
     *
     * * No signal event loop handler because we assume a single thread
     * * No register resources handler because resources are registered by the
     * resource initializers themselves
     */
    static const oc_handler_t handler = {.init =
                                             _nexus_channel_core_internal_init,
                                         .signal_event_loop = NULL,
                                         .register_resources = NULL,
                                         .requests_entry = NULL};
    if (oc_main_init(&handler) != 0)
    {
        // return early if we could not initialize OC library.
        return false;
    }
    else
    {
        /** Initialize each Nexus Channel module that is included. Each module
         * registers its own resources via `nx_channel_register_resource`.
         */
    #if NEXUS_CHANNEL_LINK_SECURITY_ENABLED
        nexus_channel_sm_init();
        #if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE
        nexus_channel_om_init();
        #endif
        // don't initialize these during unit tests, so we can test Nexus common
        // independently
        #ifndef NEXUS_DEFINED_DURING_TESTING
        nexus_channel_res_link_hs_init();
        nexus_channel_link_manager_init();
            // Note: Because PAYG Credit depends on link information, it must
            // be initialized after the link modules are initialized
            #if NEXUS_CHANNEL_USE_PAYG_CREDIT_RESOURCE
        nexus_channel_res_payg_credit_init();
            #endif // NEXUS_CHANNEL_USE_PAYG_CREDIT_RESOURCE
        #endif // NEXUS_DEFINED_DURING_TESTING
    #endif // NEXUS_CHANNEL_LINK_SECURITY_ENABLED
        return true;
    }
}

void nexus_channel_core_shutdown(void)
{
    oc_main_shutdown();
    #if NEXUS_CHANNEL_LINK_SECURITY_ENABLED
    nexus_channel_sm_free_all_nexus_resource_methods();
    #endif
}

/**Process any pending activity from Nexus channel submodules.
 *
 * Called inside `nx_common_process()`.
 *
 * \param seconds_elapsed seconds since this function was previously called
 * \return seconds until this function must be called again
 */
uint32_t nexus_channel_core_process(uint32_t seconds_elapsed)
{
    uint32_t min_sleep = NEXUS_COMMON_IDLE_TIME_BETWEEN_PROCESS_CALL_SECONDS;
    // Execute any OC/IoTivity processes until completion
    // oc_clock_time_t is a typecast for uint64_t
    const oc_clock_time_t secs_until_next_oc_process = oc_main_poll();

    // If the next timer is more than UINT32_MAX in the future, don't modify
    // our callback (we will callback sooner than that)
    //
    // if `oc_main_poll` returns `0`, there are no pending event timers
    // and the IoTivity core is idle.
    if (secs_until_next_oc_process != 0 &&
        secs_until_next_oc_process < UINT32_MAX)
    {
        min_sleep = (uint32_t) secs_until_next_oc_process;
    }
    #if NEXUS_CHANNEL_LINK_SECURITY_ENABLED
    min_sleep =
        u32min(min_sleep, nexus_channel_res_link_hs_process(seconds_elapsed));
    // XXX send pending handshakes out
    min_sleep =
        u32min(min_sleep, nexus_channel_link_manager_process(seconds_elapsed));
    #else
    (void) seconds_elapsed;
    #endif // NEXUS_CHANNEL_LINK_SECURITY_ENABLED
    return min_sleep;
}

nx_channel_error
nx_channel_register_resource(const struct nx_channel_resource_props* props)
{
    // `name` argument is not used, so set to NULL
    oc_resource_t* res = oc_new_resource(NULL,
                                         props->uri,
                                         NEXUS_CHANNEL_MAX_RTS_PER_RES,
                                         NEXUS_CHANNEL_NEXUS_DEVICE_ID);

    if (res == NULL)
    {
        return NX_CHANNEL_ERROR_UNSPECIFIED;
    }

    // bind resource type
    oc_resource_bind_resource_type(res, props->resource_type);
    res->rtr = props->rtr;

    // extract resource interfaces from input array and bind
    for (uint8_t i = 0; i < props->num_interfaces; i++)
    {
        oc_resource_bind_resource_interface(res, props->if_masks[i]);

        // set default mask to the first entry in the array
        if (i == 0)
        {
            oc_resource_set_default_interface(res, props->if_masks[i]);
        }
    }

    // Set method handlers if present
    if (props->get_handler != NULL)
    {
        oc_resource_set_request_handler(res, OC_GET, props->get_handler, NULL);
    }
    if (props->post_handler != NULL)
    {
        oc_resource_set_request_handler(
            res, OC_POST, props->post_handler, NULL);
    }

    bool success = nexus_add_resource(res);

    #if NEXUS_CHANNEL_LINK_SECURITY_ENABLED
    // if secured, attempt to store the resource method security configuration
    if (success)
    {
        if (props->get_secured &&
            nexus_channel_sm_nexus_resource_method_new(res, OC_GET) == NULL)
        {
            // unset the resource request handler
            OC_WRN("could not set the resource method security");
            oc_resource_set_request_handler(res, OC_GET, NULL, NULL);
            success = false;
        }
        if (props->post_secured &&
            nexus_channel_sm_nexus_resource_method_new(res, OC_POST) == NULL)
        {
            // unset the resource request handler
            OC_WRN("could not set the resource method security");
            oc_resource_set_request_handler(res, OC_POST, NULL, NULL);
            success = false;
        }
    }
    #else
    NEXUS_ASSERT(
        !props->get_secured && !props->post_secured,
        "Security options not compiled in, cannot secure resource method");
    #endif // NEXUS_CHANNEL_LINK_SECURITY_ENABLED

    if (success)
    {
        return NX_CHANNEL_ERROR_NONE;
    }
    else
    {
        OC_WRN("Unable to add resource successfully.");
        return NX_CHANNEL_ERROR_UNSPECIFIED;
    }
}

nx_channel_error
nx_channel_register_resource_handler(const char* uri,
                                     oc_method_t method,
                                     oc_request_callback_t handler,
                                     bool secured)
{
    oc_resource_t* res = oc_ri_get_app_resource_by_uri(
        uri, strlen(uri), NEXUS_CHANNEL_NEXUS_DEVICE_ID);

    nx_channel_error result = NX_CHANNEL_ERROR_UNSPECIFIED;

    if (res != NULL)
    {
        result =
            nexus_channel_set_request_handler(res, method, handler, secured);
    }

    return result;
}

    #if NEXUS_CHANNEL_LINK_SECURITY_ENABLED
        #if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE

static bool _nexus_channel_core_apply_origin_command_generic_controller_action(
    const struct nexus_channel_om_controller_action_body* action_body)
{
    bool success = false;
    if (action_body->action_type ==
        NEXUS_CHANNEL_ORIGIN_COMMAND_UNLINK_ALL_LINKED_ACCESSORIES)
    {
        PRINT("nx_channel_common: Processing link command 'Unlink all/clear "
              "links'...\n");
        // will assume success (link manager should never fail to delete
        // all links)
        nexus_channel_link_manager_clear_all_links();
        success = true;
    }
    // Handle other cases in future (e.g.
    // `NEXUS_CHANNEL_ORIGIN_COMMAND_UNLOCK_ALL_LINKED_ACCESSORIES`)
    return success;
}

bool nexus_channel_core_apply_origin_command(
    const struct nexus_channel_om_command_message* om_message)
{
    bool result = false;
    switch (om_message->type)
    {
        case NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3:
            PRINT(
                "nx_channel_common: Processing link command 'Create Accessory "
                "Link Mode 3'...\n");
            result = nexus_channel_res_link_hs_link_mode_3(
                &om_message->body.create_link);
            break;

        case NEXUS_CHANNEL_OM_COMMAND_TYPE_GENERIC_CONTROLLER_ACTION:
            result =
                _nexus_channel_core_apply_origin_command_generic_controller_action(
                    &om_message->body.controller_action);
            break;

        case NEXUS_CHANNEL_OM_COMMAND_TYPE_ACCESSORY_ACTION_UNLOCK:
            /* At this point in time, we know the 'full' accessory ID
            // XXX pass this information to the PAYG credit manager to
            // 'unlock' that specific accessory
            om_message->body.accessory_action.computed_accessory_id;
            */

            break;

        case NEXUS_CHANNEL_OM_COMMAND_TYPE_ACCESSORY_ACTION_UNLINK:
            /* At this point in time, we know the 'full' accessory ID
            // XXX pass this information to the Link manager to 'unlink'
            // this specific accessory.
            om_message->body.accessory_action.computed_accessory_id;
            */
            break;

        case NEXUS_CHANNEL_OM_COMMAND_TYPE_INVALID:
        // intentional fallthrough
        default:
            NEXUS_ASSERT_FAIL_IN_DEBUG_ONLY(0, "Should never reach here");
            break;
    }
    // 'true' means the command may succeed, 'false' was not attempted.
    return result;
}
        #endif // NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE
    #endif // NEXUS_CHANNEL_LINK_SECURITY_ENABLED

// May be able to entirely remove clock and timer APIs from IoTivity Lite
// core, as we aren't using observability or confirmable messages currently.
void oc_clock_init(void)
{
    // Do nothing
}

oc_clock_time_t oc_clock_time(void)
{
    // will return value in seconds
    return (oc_clock_time_t) nexus_common_uptime();
}

#endif // NEXUS_CHANNEL_CORE_ENABLED
