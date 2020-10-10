/** \file
 * Nexus Channel PAYG Credit OCF Resource (Implementation)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "src/nexus_channel_res_payg_credit.h"
#include "include/nxp_channel.h"
#include "include/nxp_core.h"
#include "src/nexus_channel_res_lm.h"
#include "src/nexus_oc_wrapper.h"
#include "utils/oc_list.h"

#if NEXUS_CHANNEL_LINK_SECURITY_ENABLED
    #if NEXUS_CHANNEL_USE_PAYG_CREDIT_RESOURCE

        // forward declarations
        #ifndef NEXUS_INTERNAL_IMPL_NON_STATIC
static void nexus_channel_res_payg_credit_get_handler(
    oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);

static void nexus_channel_res_payg_credit_post_handler(
    oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);
        #endif

        /* Value of 'remaining' credit signifying that a device is unlocked.
         *
         * A device may be assigned any value of PAYG credit from 0 to
         * (UINT32_MAX - 1), but (UINT32_MAX) represents the special 'device is
         * unlocked' (PAYG unrestricted) case.
         */
        #define NXP_CHANNEL_PAYG_CREDIT_REMAINING_UNLOCKED_SENTINEL_VALUE      \
            UINT32_MAX

// property strings ('short names')
const char* PAYG_CREDIT_REMAINING_SHORT_PROP_NAME = "re";
const char* PAYG_CREDIT_UNITS_SHORT_PROP_NAME = "un";
const char* PAYG_CREDIT_MODE_SHORT_PROP_NAME = "mo";
const char* PAYG_CREDIT_SUPPORTED_MODES_SHORT_PROP_NAME = "sM";

        // Global constants known at compile time
        #if (NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE &&                          \
             NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE)
static const uint8_t _supported_modes[4] = {
    NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_DISCONNECTED,
    NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_LEADING,
    NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_FOLLOWING,
    NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_RELAYING};
        #elif NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE
static const uint8_t _supported_modes[2] = {
    NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_DISCONNECTED,
    NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_LEADING};
        #elif NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE
static const uint8_t _supported_modes[2] = {
    NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_DISCONNECTED,
    NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_FOLLOWING};
        #else
            #error "Unable to determine what PAYG credit modes are supporetd..."
        #endif

// Units that this device reports credit in
static const char* _units = "seconds";

static struct
{
    // units of credit remaining
    uint32_t remaining;
    enum nexus_channel_payg_credit_operating_mode mode;
} _this;

// update with latest values from implementing product
static uint32_t _nexus_channel_res_payg_credit_get_latest(void)
{
    if (nxp_core_payg_state_get_current() == NXP_CORE_PAYG_STATE_UNLOCKED)
    {
        return NXP_CHANNEL_PAYG_CREDIT_REMAINING_UNLOCKED_SENTINEL_VALUE;
    }
    return nxp_core_payg_credit_get_remaining();
}

// handle a change triggered by a POST to this endpoint
// assumes `_this.remaining` has been updated
static void _nexus_channel_payg_credit_update_from_post(uint32_t new_remaining)
{
    _this.remaining = new_remaining;
    if (_this.remaining !=
        NXP_CHANNEL_PAYG_CREDIT_REMAINING_UNLOCKED_SENTINEL_VALUE)
    {
        (void) nxp_channel_payg_credit_set(_this.remaining);
    }
    else
    {
        (void) nxp_channel_payg_credit_unlock();
    }
}

NEXUS_IMPL_STATIC enum nexus_channel_payg_credit_operating_mode
_nexus_channel_res_payg_credit_get_credit_operating_mode(void)
{
    const bool has_controller =
        nexus_channel_link_manager_has_linked_controller();
    const bool has_accessory =
        nexus_channel_link_manager_has_linked_accessory();
    if (has_accessory && has_controller)
    {
        return NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_RELAYING;
    }
    else if (has_accessory)
    {
        // leading at least one other accessory
        return NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_LEADING;
    }
    else if (has_controller)
    {
        // led by a controller
        return NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_FOLLOWING;
    }
    else
    {
        return NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_DISCONNECTED;
    }
}

void nexus_channel_res_payg_credit_init(void)
{
    _this.mode = _nexus_channel_res_payg_credit_get_credit_operating_mode();
    const uint32_t stored_remaining =
        _nexus_channel_res_payg_credit_get_latest();

    switch (_this.mode)
    {
        case NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_DISCONNECTED:
            if (NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE)
            {
                // controller, remaining credit is stored credit
                _this.remaining = stored_remaining;
            }
            else if (stored_remaining ==
                     NXP_CHANNEL_PAYG_CREDIT_REMAINING_UNLOCKED_SENTINEL_VALUE)
            {
                _this.remaining = stored_remaining;
            }
            else
            {
                // only controller-capable devices maintain nonzero credit
                // if not linked, *or* this is an accessory device that
                // is already unlocked
                _this.remaining = 0;
                nxp_channel_payg_credit_set(_this.remaining);
            }
            break;

        case NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_RELAYING:
        // intentional fallthrough, pull latest credit from NV
        case NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_LEADING:
        // intentional fallthrough, pull latest credit from NV
        case NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_FOLLOWING:
            _this.remaining = stored_remaining;
            break;

        default:
            NEXUS_ASSERT(0, "Should not occur");
            break;
    }

    const oc_interface_mask_t if_mask_arr[] = {OC_IF_RW, OC_IF_BASELINE};
    const struct nx_channel_resource_props pc_props = {
        .uri = "/c",
        .resource_type = "angaza.com.nexus.payg_credit",
        .rtr = 65000,
        .num_interfaces = 2,
        .if_masks = if_mask_arr,
        // XXX make GET handler optional to save space
        .get_handler = nexus_channel_res_payg_credit_get_handler,
        .get_secured = false,
        .post_handler = nexus_channel_res_payg_credit_post_handler,
        .post_secured = true};

        #ifdef NEXUS_DEFINED_DURING_TESTING
    const nx_channel_error result =
        #endif // ifdef NEXUS_DEFINED_DURING_TESTING
        nx_channel_register_resource(&pc_props);
        #ifdef NEXUS_DEFINED_DURING_TESTING
    NEXUS_ASSERT(result == NX_CHANNEL_ERROR_NONE,
                 "Unexpected error registering resource");
        #endif
}

        #ifdef NEXUS_DEFINED_DURING_TESTING
uint32_t _nexus_channel_payg_credit_remaining_credit(void)
{
    return _this.remaining;
}

        #endif

/**
 * GET method for PAYG credit resource.
 *
 * Resource Description:
 * This resource indicates the remaining PAYG (pay-as-you-go) credit of
 * a specific device. Credit may be time-based or usage-based.
 *
 * 'Dependent' mode implies another device controls the PAYG credit on this
 * device, 'Independent' implies that this device controls its own credit
 * (and might control other dependent devices).
 *
 * See also -
 * * angaza.com.nexus.channel.link - Information on Nexus channel links
 *
 * \param request the request representation.
 * \param interfaces the interface used for this call
 * \param user_data the user data.
 */
NEXUS_IMPL_STATIC void nexus_channel_res_payg_credit_get_handler(
    oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
    (void) user_data; /* variable not used */

    _this.remaining = _nexus_channel_res_payg_credit_get_latest();
    _this.mode = _nexus_channel_res_payg_credit_get_credit_operating_mode();

    PRINT("-- payg_credit GET: interface %d\n", interfaces);
    oc_rep_begin_root_object();
    switch (interfaces)
    {
        case OC_IF_BASELINE:
            PRINT("   Adding Baseline info\n");
            oc_process_baseline_interface(request->resource);
        /* fall through */
        case OC_IF_RW:
            /* property (integer) 'mode' */
            oc_rep_set_int(root, mo, _this.mode);
            /* property (integer) 'remaining' */
            oc_rep_set_int(root, re, _this.remaining);
            /* property (array of strings) 'supportedModes' */
            oc_rep_open_array(root, sM);
            for (uint8_t i = 0; i < ((uint8_t) sizeof(_supported_modes) /
                                     (sizeof(_supported_modes[0])));
                 i++)
            {
                oc_rep_add_int(sM, _supported_modes[i]);
            }
            oc_rep_close_array(root, sM);
            /* property (string) 'units' */
            oc_rep_set_text_string(root, un, _units);
            break;
        default:
            break;
    }
    oc_rep_end_root_object();
    oc_send_response(request, OC_STATUS_OK);
    PRINT("-- End payg_credit GET\n");
}

/**
 * post method for PAYG credit resource.
 * The function has as input the request body, which are the input values of the
 * POST method.
 * The input values (as a set) are checked if all supplied values are correct.
 * If the input values are correct, they will be assigned to the global property
 * values.
 * Resource Description:
 * Sets the remaining PAYG credit of the device. If the endpoint
 * determines that the requesting device is not authorized to control
 * this device, an error response will be returned.
 *
 * \param request the request representation.
 * \param interfaces the used interfaces during the request.
 * \param user_data the supplied user data.
 */
NEXUS_IMPL_STATIC void nexus_channel_res_payg_credit_post_handler(
    oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
    (void) interfaces;
    (void) user_data;
    bool error_state = true;
    uint32_t new_remaining;
    PRINT("-- payg_credit POST:\n");

    const oc_rep_t* rep = request->request_payload;

    // Note: This endpoint relies on Nexus Channel security to screen out
    // unauthorized POST requests
    while (rep != NULL)
    {
        PRINT("key: (check) %s \n", oc_string(rep->name));
        if (strcmp(oc_string(rep->name),
                   PAYG_CREDIT_REMAINING_SHORT_PROP_NAME) == 0)
        {
            /* property "remaining" of type integer exist in payload */
            if (rep->type != OC_REP_INT || rep->value.integer > UINT32_MAX)
            {
                PRINT("   property 'remaining' is not of type int %d \n",
                      rep->type);
            }
            else
            {
                error_state = false;
                new_remaining = (uint32_t) rep->value.integer;
            }
        }
        else
        {
            // only expect 'remaining' to be sent in a POST
            PRINT("    received unexpected property in POST\n");
        }
        rep = rep->next;
    }
    /* if the input is ok, then process the input document and assign the
     * new variables */
    if (error_state == false)
    {
        _nexus_channel_payg_credit_update_from_post(new_remaining);

        oc_rep_begin_root_object();

        oc_rep_set_int(root, re, _this.remaining);
        oc_rep_set_text_string(root, un, _units);
        oc_rep_end_root_object();

        oc_send_response(request, OC_STATUS_CHANGED);
    }
    else
    {
        PRINT("  Returning Error \n");
        oc_send_response(request, OC_STATUS_BAD_REQUEST);
    }
    PRINT("-- End post_c\n");
}

    #endif /* NEXUS_CHANNEL_USE_PAYG_CREDIT_RESOURCE */
#endif /* NEXUS_CHANNEL_LINK_SECURITY_ENABLED */
