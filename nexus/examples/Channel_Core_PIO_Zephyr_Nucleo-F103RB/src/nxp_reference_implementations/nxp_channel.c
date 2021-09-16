/* \file nxp_channel.c
 * \brief Example implementation of functions specified by
 * 'nexus/include/nxp_channel.h'
 * \author Angaza \copyright 2021 Angaza, Inc
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 *
 * Contains reference implementations of Channel-specific functions that
 * Nexus Library requires in order to function, such as:
 *
 * * Notify product of Channel security events (link, unlink, handshake, etc)
 * * Request security key from product for use in verifying Channel Link
 * requests
 * * Send data to another device on the network (given its Nexus ID)
 */

#include "nxp_channel.h"

// implementation-specific random number generation (for `sys_rand32_get`)
#include <random/rand32.h>

// Product-side code to access stored Nexus ID/key values
#include "product_nexus_identity.h"

// Zephyr logging for easier demonstration purposes
#include <logging/log.h>
#include <zephyr.h>
LOG_MODULE_REGISTER(nxp_channel);

//
// 'nxp_channel' functions
//
uint32_t nxp_channel_random_value(void)
{
    // https://docs.zephyrproject.org/latest/reference/random/index.html?highlight=random#c.sys_rand32_get
    return sys_rand32_get();
}

struct nx_id nxp_channel_get_nexus_id(void)
{
    const struct nx_id* id_ptr = product_nexus_identity_get_nexus_id();
    return *id_ptr;
}

struct nx_common_check_key nxp_channel_symmetric_origin_key(void)
{
    const struct nx_common_check_key* const channel_key_ptr =
        product_nexus_identity_get_nexus_channel_secret_key();
    return *channel_key_ptr;
}

void nxp_channel_notify_event(enum nxp_channel_event_type event)
{
    switch (event)
    {
        case NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_ACCESSORY:
            LOG_INF(
                "New link established as *Accessory* device. Total links: %d\n",
                nx_channel_link_count());
            break;

        case NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_CONTROLLER:
            LOG_INF("New link established as *Controller* device. Total links: "
                    "%d\n",
                    nx_channel_link_count());
            break;

        case NXP_CHANNEL_EVENT_LINK_DELETED:
            LOG_INF("Nexus Channel link deleted. Total links: %d\n",
                    nx_channel_link_count());
            break;

        case NXP_CHANNEL_EVENT_LINK_HANDSHAKE_STARTED:
            LOG_INF("Establishing new link to an accessory...");
            break;

        case NXP_CHANNEL_EVENT_LINK_HANDSHAKE_TIMED_OUT:
            LOG_INF("Timed out attempting to establish link to an accessory.");
            break;

        default:
            // should not reach here
            assert(false);
            break;
    }
}