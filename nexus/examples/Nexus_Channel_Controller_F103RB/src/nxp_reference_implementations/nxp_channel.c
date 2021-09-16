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

// Product-side code that allows for sending of Nexus Channel messages
#include "product_data_link.h"

// For allowing credit updates over Channel
#include "product_payg_state_manager.h"

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

// Product-specific implementation of `network_send`, used by Nexus Channel
// Send bytes to a destination address. Source and destination address must
// be included in the transmitted payload 'over the wire'
nx_channel_error nxp_channel_network_send(const void* const bytes_to_send,
                                          uint32_t bytes_count,
                                          const struct nx_id* const source,
                                          const struct nx_id* const dest,
                                          bool is_multicast)
{
    // LOG_INF("Sending Nexus Channel CoAP message of %d bytes\n", bytes_count);

    LOG_HEXDUMP_INF(bytes_to_send, bytes_count, "[Outbound] data:");

    bool success = false;

    LOG_INF("[Outbound] Nexus ID SRC = [Authority ID 0x%04X, Device ID 0x%08X]",
            source->authority_id,
            source->device_id);

    if (is_multicast)
    {
        LOG_INF("[Outbound] Nexus ID DEST = MULTICAST");
    }
    else
    {
        LOG_INF("[Outbound] Nexus ID DEST = [Authority ID 0x%04X, Device ID "
                "0x%08X]",
                dest->authority_id,
                dest->device_id);
    }
    success = product_data_link_send(dest, source, bytes_to_send, bytes_count);

    if (success)
    {
        return NX_CHANNEL_ERROR_NONE;
    }
    return NX_CHANNEL_ERROR_UNSPECIFIED;
}

nx_channel_error nxp_channel_payg_credit_set(uint32_t remaining)
{
    LOG_INF("[Channel] Setting remaining credit=%d seconds", remaining);
    product_payg_state_manager_set_credit(remaining);
    return NX_CHANNEL_ERROR_NONE;
}

nx_channel_error nxp_channel_payg_credit_unlock(void)
{
    LOG_INF("[Channel] PAYG unlocking this device");
    product_payg_state_manager_unlock();
    return NX_CHANNEL_ERROR_NONE;
}
