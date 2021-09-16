/** \file product_data_link_layer.h
 * \brief A trivial demo 'data link layer' to transmit/receive bytes on UART
 * \author Angaza
 * \copyright 2021 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 *
 * This module provides a basic interface to serialize/deserialize data
 * sent over the UART connection to another device.
 * It provides the following capabilities that Nexus Channel requires:
 *
 * * Ability to send up to 128 byte messages
 * * Ability to specify source address Nexus ID for a message
 * * Ability to specify destination address Nexus ID for a message
 *
 * This module is responsible for taking the source Nexus ID, destination
 * Nexus ID, message to send, and serializing them into a stream of raw
 * bytes to send.
 *
 * This module is also responsible for deserializing incoming streams of
 * raw bytes into source Nexus ID, destination Nexus ID, and message received.
 *
 * This module sits between the Nexus Channel functionality and the `phy_uart`
 * module:
 *
 *  [nxp_channel] <-> [product_data_link_layer] <-> [phy_uart]
 *
 * In a real product, this `product_data_link_layer` functionality and
 * `phy_uart` would usually be provided by a connectivity solution
 * such as OpenPaygo Link, Bluetooth, CAN, or similar, which can
 * provide routing to specific connected devices.
 */

#ifndef PRODUCT_DATA_LINK__H
#define PRODUCT_DATA_LINK__H

#ifdef __cplusplus
extern "C" {
#endif

#include <nx_channel.h>
#include <stdbool.h>
#include <stdint.h>

// Signature of a function to handle received Nexus Channel messages
typedef nx_channel_error (*product_data_link_rx_data_handler)(
    const void* data, uint32_t len, const struct nx_id* const source);

/** @brief product_data_link_init
 *
 * Initialize data bus link, and prepare to send/receive
 * messages. Registers a function to process received Nexus
 * Channel messages.
 *
 * @param[in] data_link_rx_handler Function to handle received Nexus Channel
 * messages
 */
void product_data_link_init(
    const product_data_link_rx_data_handler data_link_rx_handler);

/**
 * @brief product_data_link_send
 *
 * Send a Nexus Channel message represented by `data` to device with
 * Nexus ID `dest_id`.
 *
 * Typically, `src_id` is determined by calling `nxp_channel_get_nexus_id`.
 *
 * @param[in] dest_id Nexus ID of device to send this message to
 * @param[in] src_id Nexus ID of sending device
 * @param[in] message Nexus Channel message to send
 * @param[in] len length of message (bytes)
 *
 * @return true if message sent successfully, false otherwise
 */
bool product_data_link_send(const struct nx_id* dest_id,
                            const struct nx_id* src_id,
                            const uint8_t* message,
                            uint8_t len);

/**
 * @brief product_data_link_send
 *
 * Send a Nexus Channel message represented by `data` to *all*
 * connected devices (broadcast). `dest_id` will be a special
 * broadcast address in this case.
 *
 * A separate function is provided to allow the product data link to
 * take special actions (if required) to send data to multiple devices
 * (instead of a single destination).
 *
 * @param[in] dest_id Nexus ID of device to send this message to
 * @param[in] src_id Nexus ID of sending device
 * @param[in] message Nexus Channel message to send
 * @param[in] len length of message (bytes)
 *
 * @return true if message sent successfully, false otherwise
 */
bool product_link_layer_send_broadcast_request(const struct nx_id* dest_id,
                                               const struct nx_id* src_id,
                                               const uint8_t* message,
                                               uint8_t len);

#ifdef __cplusplus
}
#endif
#endif // PRODUCT_DATA_LINK__H