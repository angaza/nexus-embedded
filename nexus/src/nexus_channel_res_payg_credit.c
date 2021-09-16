/** \file
 * Nexus Channel PAYG Credit OCF Resource (Implementation)
 * \author Angaza
 * \copyright 2021 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "src/nexus_channel_res_payg_credit.h"
#include "include/nxp_channel.h"
#include "include/nxp_common.h"
#include "src/nexus_channel_res_lm.h"
#include "src/nexus_common_internal.h"
#include "src/nexus_oc_wrapper.h"

#if NEXUS_CHANNEL_LINK_SECURITY_ENABLED
    #if NEXUS_CHANNEL_USE_PAYG_CREDIT_RESOURCE

        #ifdef CEEDLING_OVERRIDE_ACCESSORY_ONLY_TEST
            #undef NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE
            #define NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE 0
        #endif

        #if NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE
// forward declarations
static void _nexus_channel_res_payg_credit_get_response_handler(
    nx_channel_client_response_t* response);
        #endif // #if NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE

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
const char* PAYG_CREDIT_CONTROLLED_IDS_LIST_SHORT_PROP_NAME = "di";

// Units that this device reports credit in
static const uint8_t _units = NEXUS_CHANNEL_PAYG_CREDIT_UNITS_SECONDS;

        #if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE
const struct nx_id NEXUS_CHANNEL_PAYG_CREDIT_SENTINEL_NULL_NEXUS_ID = {0, 0};
        #endif // #if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE

static struct
{
    // units of credit remaining
    uint32_t remaining;
    enum nexus_channel_payg_credit_operating_mode mode;
        #if NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE
    // Seconds elapsed since this device received a credit update from a
    // linked controller (applicable in follower mode)
    uint32_t follower_mode_seconds_since_credit_updated;
        #endif
        #if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE
    // updated in the `process` loop to determine when to send credit updates
    // to linked accessories on PAYG state transition
    enum nxp_common_payg_state last_payg_state;
    // Nexus ID of last device that received a PAYG credit update
    struct nx_id last_updated_nexus_id;
    uint32_t seconds_since_last_post;
    // arbitrary 'first' Nexus ID to update when cycling through linked devices
    struct nx_id cycle_first_nexus_id;
    uint32_t seconds_since_last_cycle_start;
        #endif // #if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE
} _this;

// update with latest values from implementing product
static uint32_t _nexus_channel_res_payg_credit_get_latest(
    const enum nxp_common_payg_state current_payg_state)
{
    if (current_payg_state == NXP_COMMON_PAYG_STATE_UNLOCKED)
    {
        return NXP_CHANNEL_PAYG_CREDIT_REMAINING_UNLOCKED_SENTINEL_VALUE;
    }
    return nxp_common_payg_credit_get_remaining();
}

NEXUS_IMPL_STATIC enum nexus_channel_payg_credit_operating_mode
_nexus_channel_res_payg_credit_get_credit_operating_mode(void)
{
    struct nx_id ignored_id;
    const bool has_controller =
        nexus_channel_link_manager_has_linked_controller(&ignored_id);
    (void) ignored_id;
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
        return NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_INDEPENDENT;
    }
}

// handle a change triggered by a POST to this endpoint, or a GET response
static void
_nexus_channel_payg_credit_update_from_post_or_get(uint32_t new_remaining)
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

        #if NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE
    if (_nexus_channel_res_payg_credit_get_credit_operating_mode() ==
        NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_FOLLOWING)
    {
        _this.follower_mode_seconds_since_credit_updated = 0;
    }
        #endif
}

// Determines the PAYG credit of the unit on boot based on the most recently
// stored credit in NV, and the current PAYG operating mode of the device.
static uint32_t _nexus_channel_res_payg_credit_calculate_initial_credit(
    const enum nexus_channel_payg_credit_operating_mode current_mode,
    const uint32_t current_remaining_credit)
{
    uint32_t credit_to_return = 0;
    switch (current_mode)
    {
        // independent, no links to other channel devices
        case NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_INDEPENDENT:
            if (NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE)
            {
                // controller, remaining credit is stored credit
                credit_to_return = current_remaining_credit;
            }
            else if (current_remaining_credit ==
                     NXP_CHANNEL_PAYG_CREDIT_REMAINING_UNLOCKED_SENTINEL_VALUE)
            {
                // an 'independent' unlocked accessory remains unlocked
                credit_to_return = current_remaining_credit;
            }
            break;

        case NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_FOLLOWING:
            // If a device is following, it has 0 credit on init unless
            // it is already unlocked
            if (current_remaining_credit ==
                NXP_CHANNEL_PAYG_CREDIT_REMAINING_UNLOCKED_SENTINEL_VALUE)
            {
                credit_to_return = current_remaining_credit;
            }
            break;

        case NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_RELAYING:
        // intentional fallthrough, rely on latest credit from NV
        case NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_LEADING:
            credit_to_return = current_remaining_credit;
            break;

        default:
            NEXUS_ASSERT(0, "Should not occur");
            break;
    }
    return credit_to_return;
}

void nexus_channel_res_payg_credit_init(void)
{
    const enum nxp_common_payg_state payg_state =
        nxp_common_payg_state_get_current();
    const uint32_t stored_remaining =
        _nexus_channel_res_payg_credit_get_latest(payg_state);
    _this.mode = _nexus_channel_res_payg_credit_get_credit_operating_mode();
    const uint32_t new_remaining =
        _nexus_channel_res_payg_credit_calculate_initial_credit(
            _this.mode, stored_remaining);

    _this.remaining = new_remaining;
    if (new_remaining != stored_remaining)
    {
        nxp_channel_payg_credit_set(_this.remaining);
    }
        #if NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE
    _this.follower_mode_seconds_since_credit_updated = 0;
        #endif // #if NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE

        #if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE
    _this.last_payg_state = payg_state;
    memcpy(&_this.last_updated_nexus_id,
           &NEXUS_CHANNEL_PAYG_CREDIT_SENTINEL_NULL_NEXUS_ID,
           sizeof(struct nx_id));
    memcpy(&_this.cycle_first_nexus_id,
           &NEXUS_CHANNEL_PAYG_CREDIT_SENTINEL_NULL_NEXUS_ID,
           sizeof(struct nx_id));
    _this.seconds_since_last_post =
        NEXUS_CHANNEL_PAYG_CREDIT_INTERVAL_BETWEEN_PAYG_CREDIT_POST_SECONDS;
    _this.seconds_since_last_cycle_start =
        NEXUS_CHANNEL_PAYG_CREDIT_POST_UPDATE_CYCLE_TIME_SECONDS;
        #endif // #if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE

    const oc_interface_mask_t if_mask_arr[] = {OC_IF_RW, OC_IF_BASELINE};
    const struct nx_channel_resource_props pc_props = {
        .uri = "/nx/pc",
        .resource_type = "angaza.com.nx.pc",
        .rtr = 401,
        .num_interfaces = 2,
        .if_masks = if_mask_arr,
        // XXX make GET handler optional to save space
        // (Cannot make it optional for controllers, as they expect
        // a secured GET from accessories on boot)
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

        #if NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE
    // When following devices first boot up, allow then to GET the
    // latest PAYG credit state from the controller.
    if ((_this.mode == NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_FOLLOWING) ||
        (_this.mode == NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_RELAYING))
    {
        struct nx_id controller_id;
        (void) nexus_channel_link_manager_has_linked_controller(&controller_id);

        // Attempt to get current PAYG credit on boot, ignore failures -
        // controller will update credit on next POST cycle
        (void) nx_channel_do_get_request_secured(
            "nx/pc",
            &controller_id,
            NULL,
            _nexus_channel_res_payg_credit_get_response_handler,
            NULL);
    }
        #endif // #if NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE
}

        #ifdef NEXUS_DEFINED_DURING_TESTING
uint32_t _nexus_channel_payg_credit_remaining_credit(void)
{
    return _this.remaining;
}
        #endif

static bool _nexus_channel_payg_credit_evaluate_payload_extract_credit(
    uint32_t* new_remaining, const oc_rep_t* rep)
{
    bool error_state = true;
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
                *new_remaining = (uint32_t) rep->value.integer;
            }
        }
        else
        {
            // only expect 'remaining' to be sent in a POST
            PRINT("    received unexpected property in POST\n");
        }
        rep = rep->next;
    }
    return error_state;
}

        #if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE
static void _nexus_channel_res_payg_credit_post_response_handler(
    nx_channel_client_response_t* response)
{
    if (memcmp(response->source,
               &_this.last_updated_nexus_id,
               sizeof(struct nx_id)) != 0)
    {
        OC_WRN("Unexpected source for response to PAYG credit POST");
    }
}
        #endif // #if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE

        #if NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE
// only used for a secured GET request on boot for accessories
static void _nexus_channel_res_payg_credit_get_response_handler(
    nx_channel_client_response_t* response)
{
    const oc_rep_t* rep = response->payload;
    uint32_t new_remaining = 0;

    bool error_state =
        _nexus_channel_payg_credit_evaluate_payload_extract_credit(
            &new_remaining, rep);

    /* if the input is ok, then process the input document and assign the
     * new variables */
    if (error_state == false)
    {
        _nexus_channel_payg_credit_update_from_post_or_get(new_remaining);
    }
    else
    {
        PRINT("  Failed to process GET response \n");
    }
}
        #endif // #if NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE

uint32_t nexus_channel_res_payg_credit_process(uint32_t seconds_elapsed)
{
    uint32_t min_sleep = NEXUS_COMMON_IDLE_TIME_BETWEEN_PROCESS_CALL_SECONDS;

    const enum nxp_common_payg_state payg_state =
        nxp_common_payg_state_get_current();
    const uint32_t latest_credit =
        _nexus_channel_res_payg_credit_get_latest(payg_state);
    _this.remaining = latest_credit;

    const enum nexus_channel_payg_credit_operating_mode current_operating_mode =
        _nexus_channel_res_payg_credit_get_credit_operating_mode();

        #if NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE
    // Logic only relevant to a device that is currently getting credit from
    // another leader/controller, and is not unlocked
    if (_this.remaining !=
        NXP_CHANNEL_PAYG_CREDIT_REMAINING_UNLOCKED_SENTINEL_VALUE)
    {
        if (current_operating_mode ==
            NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_FOLLOWING)
        {
            // update seconds since credit last updated
            _this.follower_mode_seconds_since_credit_updated += seconds_elapsed;

            // haven't heard from controller in too long, erase credit
            if (_this.follower_mode_seconds_since_credit_updated >=
                NEXUS_CHANNEL_PAYG_CREDIT_FOLLOWER_MAX_TIME_BETWEEN_UPDATES_SECONDS)
            {
                nxp_channel_payg_credit_set(0);
                _this.remaining = 0;
                _this.follower_mode_seconds_since_credit_updated = 0;
            }
            else
            {
                min_sleep =
                    NEXUS_CHANNEL_PAYG_CREDIT_FOLLOWER_MAX_TIME_BETWEEN_UPDATES_SECONDS -
                    _this.follower_mode_seconds_since_credit_updated;
            }
        }
        // If we changed from following to independent (e.g. lost a link)
        // and are not credit 'unlocked', erase credit
        else if ((current_operating_mode ==
                  NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_INDEPENDENT) &&
                 (_this.mode ==
                  NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_FOLLOWING))
        {
            nxp_channel_payg_credit_set(0);
            _this.remaining = 0;
        }
    }
        #endif // #if NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE

    _this.mode = current_operating_mode;

        #if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE
    const uint8_t num_links = nexus_channel_link_manager_accessory_link_count();
    // `_this.last_payg_state` is *only* updated in this process function,
    // and is used to detect if the PAYG state has changed since the last
    // time the process function was called. If it has, reset the link
    // cycle timers to immediately send messages out.
    if (payg_state != _this.last_payg_state)
    {
        _this.seconds_since_last_post =
            NEXUS_CHANNEL_PAYG_CREDIT_INTERVAL_BETWEEN_PAYG_CREDIT_POST_SECONDS;
        _this.seconds_since_last_cycle_start =
            NEXUS_CHANNEL_PAYG_CREDIT_POST_UPDATE_CYCLE_TIME_SECONDS;
    }
    _this.last_payg_state = payg_state;

    bool next_id_found = false;
    struct nx_id next_id;

    // skip accessory and controller logic if we have no links
    if (num_links == 0)
    {
        return min_sleep;
    }

    // if we have any IDs already stored, ensure that they are still
    // linked before proceeding.
    bool stored_ids_still_linked = true;
    struct nexus_channel_link_t ignored_link_data;
    if (!nexus_channel_link_manager_link_from_nxid(&_this.last_updated_nexus_id,
                                                   &ignored_link_data))
    {
        stored_ids_still_linked = false;
    }
    else if (!nexus_channel_link_manager_link_from_nxid(
                 &_this.cycle_first_nexus_id, &ignored_link_data))
    {
        stored_ids_still_linked = false;
    }
    (void) ignored_link_data;

    if (!stored_ids_still_linked)
    {
        // if either of our stored IDs no longer represent a link,
        // reset both to the sentinel value, and reset our cycle parameters
        memcpy(&_this.last_updated_nexus_id,
               &NEXUS_CHANNEL_PAYG_CREDIT_SENTINEL_NULL_NEXUS_ID,
               sizeof(struct nx_id));
        memcpy(&_this.cycle_first_nexus_id,
               &NEXUS_CHANNEL_PAYG_CREDIT_SENTINEL_NULL_NEXUS_ID,
               sizeof(struct nx_id));
        _this.seconds_since_last_post =
            NEXUS_CHANNEL_PAYG_CREDIT_INTERVAL_BETWEEN_PAYG_CREDIT_POST_SECONDS;
        _this.seconds_since_last_cycle_start =
            NEXUS_CHANNEL_PAYG_CREDIT_POST_UPDATE_CYCLE_TIME_SECONDS;
    }

    // 1 more or links exist, and we havent updated any Nexus ID yet
    if (memcmp(&_this.last_updated_nexus_id,
               &NEXUS_CHANNEL_PAYG_CREDIT_SENTINEL_NULL_NEXUS_ID,
               sizeof(struct nx_id)) == 0)
    {
        next_id_found =
            nexus_channel_link_manager_next_linked_accessory(NULL, &next_id);
    }

    // 1 or more links exist, and this is not the first update we've performed
    else
    {
        next_id_found = nexus_channel_link_manager_next_linked_accessory(
            &_this.last_updated_nexus_id, &next_id);
    }

    NEXUS_ASSERT(next_id_found, "More than 0 links, but no linked IDs found");
    (void) next_id_found;

    _this.seconds_since_last_post += seconds_elapsed;
    _this.seconds_since_last_cycle_start += seconds_elapsed;

    // If we're about to send to the 'first' Nexus ID in the cycle,
    // ensure we have waited the inter-cycle time.
    if (memcmp(&_this.cycle_first_nexus_id, &next_id, sizeof(struct nx_id)) ==
        0)
    {
        if (_this.seconds_since_last_cycle_start <
            NEXUS_CHANNEL_PAYG_CREDIT_POST_UPDATE_CYCLE_TIME_SECONDS)
        {
            return NEXUS_CHANNEL_PAYG_CREDIT_POST_UPDATE_CYCLE_TIME_SECONDS -
                   _this.seconds_since_last_cycle_start;
        }
        // If we've waited the inter-cycle time, reset counter between
        // cycles to 0, and continue to send a POST
        _this.seconds_since_last_cycle_start = 0;
    }

    // Return early if we haven't waited the minimum time between
    // POST requests, and do not update the `last_updated_nexus_id`
    if (_this.seconds_since_last_post <
        NEXUS_CHANNEL_PAYG_CREDIT_INTERVAL_BETWEEN_PAYG_CREDIT_POST_SECONDS)
    {
        return NEXUS_CHANNEL_PAYG_CREDIT_INTERVAL_BETWEEN_PAYG_CREDIT_POST_SECONDS -
               _this.seconds_since_last_post;
    }

    // Update the Nexus ID of the device we are trying to sync
    memcpy(&_this.last_updated_nexus_id, &next_id, sizeof(struct nx_id));
    _this.seconds_since_last_post = 0;

    // Attempt to update the device. If we fail, we will ignore the failure
    // and continue looping to the next device. We do not process the response
    // for the POST.
    if (nx_channel_init_post_request(
            "nx/pc",
            &next_id,
            NULL,
            _nexus_channel_res_payg_credit_post_response_handler,
            NULL) != NX_CHANNEL_ERROR_NONE)
    {
        OC_WRN("Unable to initialize PAYG credit POST");
        return NEXUS_CHANNEL_PAYG_CREDIT_INTERVAL_BETWEEN_PAYG_CREDIT_POST_SECONDS;
    }
    oc_rep_begin_root_object();
    // updated credit
    oc_rep_set_uint(root, re, _this.remaining);
    oc_rep_end_root_object();

    if (nx_channel_do_post_request_secured() != NX_CHANNEL_ERROR_NONE)
    {
        OC_WRN("Unable to send PAYG credit POST");
        return NEXUS_CHANNEL_PAYG_CREDIT_INTERVAL_BETWEEN_PAYG_CREDIT_POST_SECONDS;
    }

    // Initialization case - we need a Nexus ID to determine when to end
    // an 'update cycle' of linked devices. Arbitrary set the device that we
    // just attempted to update as the 'first' in the cycle.
    if (memcmp(&_this.cycle_first_nexus_id,
               &NEXUS_CHANNEL_PAYG_CREDIT_SENTINEL_NULL_NEXUS_ID,
               sizeof(struct nx_id)) == 0)
    {
        memcpy(&_this.cycle_first_nexus_id, &next_id, sizeof(struct nx_id));
        _this.seconds_since_last_cycle_start = 0;
    }
    NEXUS_ASSERT((memcmp(&_this.cycle_first_nexus_id,
                         &NEXUS_CHANNEL_PAYG_CREDIT_SENTINEL_NULL_NEXUS_ID,
                         sizeof(struct nx_id)) != 0),
                 "Exiting `payg_credit_process`, but `cycle_first_nexus_id` is "
                 "undefined");

    min_sleep =
        NEXUS_CHANNEL_PAYG_CREDIT_INTERVAL_BETWEEN_PAYG_CREDIT_POST_SECONDS;
        #endif // #if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE
    return min_sleep;
}

/**
 * GET method for PAYG credit resource.
 *
 * Resource Description:
 * This resource indicates the remaining PAYG (pay-as-you-go) credit of
 * a specific device. Credit may be time-based or usage-based.
 *
 * 'Independent' mode implies that this device controls its own credit
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

    const enum nxp_common_payg_state payg_state =
        nxp_common_payg_state_get_current();
    _this.remaining = _nexus_channel_res_payg_credit_get_latest(payg_state);
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
            /* property (int) 'units' */
            oc_rep_set_int(root, un, _units);
            /* property (list) 'controlled devices' */
            oc_rep_open_array(root, di);
        #if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE
            if (_this.mode == NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_LEADING)
            {
                // iterate through linked accessory devices
                // XXX assumes that all linked accessories support PAYG credit
                struct nx_id first_id;
                struct nx_id prev_id;
                struct nx_id next_id;
                bool complete = false;
                bool first_run = true;

                uint16_t authority_id_be;
                uint32_t device_id_be;
                uint8_t linked_nexus_id_be[sizeof(struct nx_id)];
                NEXUS_STATIC_ASSERT(sizeof(struct nx_id) == 6,
                                    "Unexpected NX ID size");

                while (!complete)
                {
                    // first iteration, populate `prev_id`
                    if (first_run)
                    {
                        if (!nexus_channel_link_manager_next_linked_accessory(
                                NULL, &prev_id))
                        {
                            // immediately complete if there are no links found
                            complete = true;
                        }
                        else
                        {
                            memcpy(&first_id, &prev_id, sizeof(struct nx_id));

                            authority_id_be =
                                nexus_endian_htobe16(prev_id.authority_id);
                            device_id_be =
                                nexus_endian_htobe32(prev_id.device_id);
                            memcpy(&linked_nexus_id_be[0], &authority_id_be, 2);
                            memcpy(&linked_nexus_id_be[2], &device_id_be, 4);
                            oc_rep_add_byte_string(
                                di, linked_nexus_id_be, sizeof(struct nx_id));
                        }
                        first_run = false;
                    }
                    // subsequent iterations
                    else
                    {
                        // terminate if no other links are found after `prev_id`
                        if (!nexus_channel_link_manager_next_linked_accessory(
                                &prev_id, &next_id))
                        {
                            complete = true;
                        }
                        // have looped around, termination condition
                        else if (memcmp(&first_id,
                                        &next_id,
                                        sizeof(struct nx_id)) == 0)
                        {
                            complete = true;
                        }
                        else
                        {
                            memcpy(&prev_id, &next_id, sizeof(struct nx_id));

                            authority_id_be =
                                nexus_endian_htobe16(prev_id.authority_id);
                            device_id_be =
                                nexus_endian_htobe32(prev_id.device_id);
                            memcpy(&linked_nexus_id_be[0], &authority_id_be, 2);
                            memcpy(&linked_nexus_id_be[2], &device_id_be, 4);
                            oc_rep_add_byte_string(
                                di, linked_nexus_id_be, sizeof(struct nx_id));
                        }
                    }
                }
            }
        #endif // #if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE
            oc_rep_close_array(root, di);
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
    // Note: This endpoint relies on Nexus Channel security to screen out
    // unauthorized POST requests

    (void) interfaces;
    (void) user_data;
    uint32_t new_remaining = 0;
    PRINT("-- payg_credit POST:\n");

    const oc_rep_t* rep = request->request_payload;

    bool error_state =
        _nexus_channel_payg_credit_evaluate_payload_extract_credit(
            &new_remaining, rep);

    /* if the input is ok, then process the input document and assign the
     * new variables */
    if (error_state == false)
    {
        _nexus_channel_payg_credit_update_from_post_or_get(new_remaining);

        oc_rep_begin_root_object();

        oc_rep_set_int(root, re, _this.remaining);
        oc_rep_set_int(root, un, _units);
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
