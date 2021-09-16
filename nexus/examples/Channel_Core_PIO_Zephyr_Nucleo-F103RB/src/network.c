/** \file network.c
 * \brief A mock implementation of networking interface to Nexus Channel
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#ifdef CHANNEL_CORE_SUPPORTED_DEMO_BUILD_ENABLED

    #include "network.h"
    #include "nx_channel.h"
    #include <stdbool.h>

    // Zephyr logging for easier demonstration purposes
    #include <logging/log.h>
    #include <zephyr.h>
LOG_MODULE_REGISTER(network);

// "Receive data" from the network-specific logic (LIN, UART, BLE, I2C, etc)
// In this case, we receive data from the Zephyr 'console'.
// Typically, `data` is extracted from another 'on the wire' packet that
// contains a check field (ensuring data integrity) as well as addressing
// information (so that the `source` nx_id can be determined).
void receive_data_from_network(void* bytes_received,
                               uint32_t bytes_count,
                               struct nx_id* source)
{
    LOG_INF("[Inbound] Received %d bytes from Nexus ID = [Authority ID "
            "0x%04X, Device ID 0x%08X]\n",
            bytes_count,
            source->authority_id,
            source->device_id);

    LOG_HEXDUMP_INF(bytes_received, bytes_count, "[Inbound] Received data:");
    LOG_INF("\n");

    nx_channel_network_receive(bytes_received, bytes_count, source);
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

    if (is_multicast)
    {
        LOG_INF("[Outbound] Nexus ID DEST = MULTICAST\n");
        // Ignoring details of multicast scope for now (can determine by looking
        // at `dest` address if required)
    }
    else
    {
        LOG_INF("[Outbound] Nexus ID DEST = [Authority ID 0x%04X, Device ID "
                "0x%08X]\n",
                dest->authority_id,
                dest->device_id);
        // Send to a single device (indicated by `dest_address`)
        // Fill in with product-specific implementation
    }
    LOG_INF(
        "[Outbound] Nexus ID SRC = [Authority ID 0x%04X, Device ID 0x%08X]\n",
        source->authority_id,
        source->device_id);

    // loopback - connect 'outbound' messages to 'inbound' for demo.
    LOG_INF("[Outbound] Looping back outbound data to inbound...");
    receive_data_from_network(
        (void*) bytes_to_send, bytes_count, (struct nx_id*) source);

    return NX_CHANNEL_ERROR_NONE;
}

#endif // #ifdef CHANNEL_CORE_SUPPORTED_DEMO_BUILD_ENABLED