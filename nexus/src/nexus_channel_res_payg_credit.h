/** \file
 * Nexus Channel PAYG Credit OCF Resource (Header)
 * \author Angaza
 * \copyright 2021 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all
 * copies or substantial portions of the Software.
 *
 * Explanation of operational modes:
 *
 *                     I AM LINKED AS AN ACCESSORY /
 *                     I EXPECT CREDIT UPDATES FROM
                       A (LINKED) CONTROLLER

                           TRUE           FALSE
                    +---------------+--------------+
                  T |               |              |
                  R |               |              |
I AM LINKED AS    U |    RELAYING   |    LEADING   |
A CONTROLLER /    E |               |              |
I UPDATE THE        |               |              |
CREDIT OF           +------------------------------+
(LINKED)          F |               |              |
ACCESSORIES       A |               |              |
                  L |   FOLLOWING   |  INDEPENDENT |
                  S |               |              |
                  E |               |              |
                    +---------------+--------------+
 *
 * Note: After being unlocked by a linked controller and then
 * unlinked, accessories will remain unlocked until re-linked.
 * Note: Relaying mode is not currently supported.
 */
#ifndef NEXUS__CHANNEL__SRC__NEXUS_CHANNEL_RES_PAYG_CREDIT__H
#define NEXUS__CHANNEL__SRC__NEXUS_CHANNEL_RES_PAYG_CREDIT__H

#include "src/internal_channel_config.h"

#if NEXUS_CHANNEL_LINK_SECURITY_ENABLED

    #ifdef __cplusplus
extern "C" {
    #endif

    #if NEXUS_CHANNEL_USE_PAYG_CREDIT_RESOURCE

        // Exposed only for unit tests to confirm resource model contents
        #ifdef NEXUS_DEFINED_DURING_TESTING
extern const char* PAYG_CREDIT_REMAINING_SHORT_PROP_NAME;
extern const char* PAYG_CREDIT_UNITS_SHORT_PROP_NAME;
extern const char* PAYG_CREDIT_MODE_SHORT_PROP_NAME;
extern const char* PAYG_CREDIT_CONTROLLED_IDS_LIST_SHORT_PROP_NAME;

// expose internal state for unit tests
uint32_t _nexus_channel_payg_credit_remaining_credit(void);
        #endif

extern const struct nx_id NEXUS_CHANNEL_PAYG_CREDIT_SENTINEL_NULL_NEXUS_ID;

        // Time between POST requests to linked accessory devices
        #define NEXUS_CHANNEL_PAYG_CREDIT_INTERVAL_BETWEEN_PAYG_CREDIT_POST_SECONDS \
            2

        // How long, in seconds, between controller/leader attempts to
        // 'synchronize' (via POST) PAYG credit to each linked accessory
        #define NEXUS_CHANNEL_PAYG_CREDIT_POST_UPDATE_CYCLE_TIME_SECONDS 25

        #if NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE
            // How long, in seconds, will an accessory/following device
            // wait for a POST from a controller before resetting credit to 0.
            // (UNLOCKED devices ignore this timeout)
            #define NEXUS_CHANNEL_PAYG_CREDIT_FOLLOWER_MAX_TIME_BETWEEN_UPDATES_SECONDS \
                (3 * NEXUS_CHANNEL_PAYG_CREDIT_POST_UPDATE_CYCLE_TIME_SECONDS)
        #endif // # if NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE

enum nexus_channel_payg_credit_operating_mode
{
    NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_INDEPENDENT = 0,
    NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_LEADING = 1,
    NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_FOLLOWING = 2,
    NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_RELAYING = 3,
};

enum nexus_channel_payg_credit_units
{
    NEXUS_CHANNEL_PAYG_CREDIT_UNITS_NONE = 0,
    NEXUS_CHANNEL_PAYG_CREDIT_UNITS_SECONDS = 1,
    NEXUS_CHANNEL_PAYG_CREDIT_UNITS_HOURS = 2,
    NEXUS_CHANNEL_PAYG_CREDIT_UNITS_DAYS = 3,
    NEXUS_CHANNEL_PAYG_CREDIT_UNITS_LITERS = 10,
    NEXUS_CHANNEL_PAYG_CREDIT_UNITS_GALLONS = 11,
    NEXUS_CHANNEL_PAYG_CREDIT_UNITS_WATT_HOURS = 20,
};

/* Initialize the Nexus Channel PAYG Credit module.
 *
 * Called on startup by `nexus_channel_core_init()`.
 */
void nexus_channel_res_payg_credit_init(void);

/* Called to perform processing for PAYG credit outside of an interrupt.
 *
 * For example, controller role devices must periodically send POST
 * requests to connected accessories.
 */
uint32_t nexus_channel_res_payg_credit_process(uint32_t seconds_elapsed);

        #ifdef NEXUS_INTERNAL_IMPL_NON_STATIC

void nexus_channel_res_payg_credit_get_handler(oc_request_t* request,
                                               oc_interface_mask_t interfaces,
                                               void* user_data);

void nexus_channel_res_payg_credit_post_handler(oc_request_t* request,
                                                oc_interface_mask_t interfaces,
                                                void* user_data);

enum nexus_channel_payg_credit_operating_mode
_nexus_channel_res_payg_credit_get_credit_operating_mode(void);

        #endif // NEXUS_INTERNAL_IMPL_NON_STATIC

    #endif // NEXUS_CHANNEL_USE_PAYG_CREDIT_RESOURCE

    #ifdef __cplusplus
}
    #endif

#endif // NEXUS_CHANNEL_LINK_SECURITY_ENABLED
#endif // NEXUS__CHANNEL__SRC__NEXUS_CHANNEL_RES_PAYG_CREDIT__H
