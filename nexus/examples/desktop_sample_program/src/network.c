/** \file network.c
 * \brief A mock implementation of networking interface to Nexus Channel
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "network.h"
#include <stdbool.h>

static bool simulate_accessory_response = false;
extern void simulate_message_link_handshake_response_accessory(
    void* data, uint32_t data_len, struct nx_ipv6_address* source_address);

// for demonstration only
void enable_simulated_accessory_response(void)
{
    simulate_accessory_response = true;
}

// for demonstration only
void disable_simulated_accessory_response(void)
{
    simulate_accessory_response = false;
}

// "Receive data" from the network-specific logic (LIN, UART, BLE, I2C, etc)
//
void receive_data_from_network(void* data,
                               uint32_t data_len,
                               struct nx_ipv6_address* source_addr)
{
    // any product-specific validation of data occurs here - if there are
    // link layer specific CRCs or headers, remove them before passing the data
    // to `nx_channel_network_receive`, which expects only application data
    // (CoAP payload meeting OCF specifications).
    //
    // In other words, data sent out by `nxp_channel_network_send` on one
    // device should be received, unmodified, by `nx_channel_network_receive`
    // on the destination device(s).
    nx_channel_network_receive(data, data_len, source_addr);
}

// Product-specific implementation of `network_send`, used by Nexus Channel
// Send bytes to a destination address. Source and destination address must
// be included in the transmitted payload 'over the wire'
nx_channel_error
nxp_channel_network_send(const void* const bytes_to_send,
                         uint32_t bytes_count,
                         const struct nx_ipv6_address* const source_address,
                         const struct nx_ipv6_address* const dest_address,
                         bool is_multicast)
{
    (void) dest_address;
    if (is_multicast)
    {
        // Send to all connected devices (dest address is a special multicast
        // address in this case)
        // Fill in with product-specific implementation
    }
    else
    {
        // Send to a single device (indicated by `dest_address`)
        // Fill in with product-specific implementation
    }

    // this section added only for demonstration in this example program.
    // Assumes that messages sent was a link handshake challenge, and a
    // response from an accessory is required.
    if (simulate_accessory_response)
    {
        simulate_message_link_handshake_response_accessory(
            (void*) bytes_to_send,
            bytes_count,
            (struct nx_ipv6_address*) source_address);
    }

    return NX_CHANNEL_ERROR_NONE;
}
