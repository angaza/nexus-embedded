/** \file product_data_link.c
 * \brief A mock implementation of physical layer between devices
 * \author Angaza
 * \copyright 2021 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 *
 * This implements a basic UART link, which allows sending and receiving
 * raw bytes between two devices. The functionality in this
 * module is used by `product_data_link.h`, which is used by Nexus Channel to
 * provide standard interaction between devices (via Nexus Channel Resources).
 */
#include "product_data_link.h"
#include "nxp_channel.h"
#include "phy_uart.h"

// Include byteordering functions from Zephyr
#include <sys/byteorder.h>

// Zephyr logging for easier demonstration purposes
#include <logging/log.h>
#include <zephyr.h>
LOG_MODULE_REGISTER(data_link);

static struct
{
    product_data_link_rx_data_handler rx_handler;

} _this;

#define PRODUCT_DATA_LINK_ADDRESS_HEADER_LEN 12
#define PRODUCT_DATA_LINK_MAX_MESSAGE_SIZE 128

// Serialization/deserialization of address to a 12 byte 'header'
// Assumes that `data` is at least 12 bytes in length
// Note: This is a demonstration header deserialization scheme,
// for production, please use a more robust link layer (such as OpenPaygo Link)

/**
 * @brief _product_data_link_deserialize_src_dest
 *
 * (Internal)
 * Given raw bytes received from the UART, extract the destination
 * Nexus ID, source Nexus ID, and Nexus message.
 *
 * The dest/source IDs take up the first 12 bytes. The remaining bytes
 * of 'data' are the Nexus message.
 *
 * Note: data *must* be at least 12 bytes long, or this will not yield
 * a valid result.
 *
 * @param[in] data received data, including 12 byte header
 * @param[out] dest_id Nexus ID of destination device (will be populated)
 * @param[out] src_id Nexus ID of source device (will be populated)
 *
 * @return true if message sent successfully, false otherwise
 */
static void _product_data_link_deserialize_src_dest(const uint8_t* data,
                                                    struct nx_id* dest_id,
                                                    struct nx_id* src_id)
{
    uint8_t* pos = (uint8_t*) data;

    // first 6 bytes are destination NX ID
    dest_id->authority_id = sys_be16_to_cpu(*((uint16_t*) pos));
    pos += (sizeof(uint16_t));
    dest_id->device_id = sys_be32_to_cpu(*((uint32_t*) pos));
    pos += (sizeof(uint32_t));
    assert(pos ==
           ((uint8_t*) (data + (PRODUCT_DATA_LINK_ADDRESS_HEADER_LEN / 2))));

    // next 6 bytes are source NX ID
    src_id->authority_id = sys_be16_to_cpu(*((uint16_t*) pos));
    pos += (sizeof(uint16_t));
    src_id->device_id = sys_be32_to_cpu(*((uint32_t*) pos));
    pos += (sizeof(uint32_t));
    assert(pos == ((uint8_t*) (data + PRODUCT_DATA_LINK_ADDRESS_HEADER_LEN)));
}

static void _product_data_link_handle_rx_bytes_from_uart(const uint8_t* data,
                                                         uint8_t len)
{
    struct nx_id src_id;
    struct nx_id dest_id;

    if (len < PRODUCT_DATA_LINK_ADDRESS_HEADER_LEN)
    {
        LOG_INF("Received invalid message, too short - ignoring");
        return;
    }

    _product_data_link_deserialize_src_dest(data, &dest_id, &src_id);
    // process the Nexus message contents, excluding the header

    // Logging for demonstration
    LOG_INF("[Inbound] Nexus ID SRC = [Authority ID 0x%04X, Device ID 0x%08X]",
            src_id.authority_id,
            src_id.device_id);
    LOG_INF("[Inbound] Nexus ID DEST = [Authority ID 0x%04X, Device ID "
            "0x%08X]",
            dest_id.authority_id,
            dest_id.device_id);

    LOG_HEXDUMP_INF(data + PRODUCT_DATA_LINK_ADDRESS_HEADER_LEN,
                    len - PRODUCT_DATA_LINK_ADDRESS_HEADER_LEN,
                    "[Inbound] data:");

    _this.rx_handler(data + PRODUCT_DATA_LINK_ADDRESS_HEADER_LEN,
                     len - PRODUCT_DATA_LINK_ADDRESS_HEADER_LEN,
                     &src_id);
}

void product_data_link_init(
    const product_data_link_rx_data_handler data_link_rx_handler)
{
    _this.rx_handler = data_link_rx_handler;
    phy_uart_init(&_product_data_link_handle_rx_bytes_from_uart);
}

/**
 * @brief _product_data_link_serialize_src_dest
 *
 * (Internal)
 * Given a destination Nexus ID, source Nexus ID, and Nexus Channel message
 * to send, serialize it into raw bytes to send on the wire.
 *
 * The dest/source IDs take up the first 12 bytes. The remaining bytes
 * of 'data' are the Nexus message.
 *
 * `out_buf` *must* have at least `nexus_msg_len` +
 * PRODUCT_DATA_LINK_ADDRESS_HEADER_LEN bytes free, or an overflow will occur.
 *
 * @param[in] dest_id Nexus ID of destination device
 * @param[in] src_id Nexus ID of source device
 * @param[in] nexus_msg Nexus request/response message
 * @param[in] nexus_msg_len length of nexus_msg (bytes)
 * @param[out] out_buf buffer to write serialized bytes to
 *
 * @return number of bytes serialized into `out_buf`
 */
static uint8_t
_product_data_link_serialize_src_dest(const struct nx_id* const dest_id,
                                      const struct nx_id* const src_id,
                                      const uint8_t* const nexus_msg,
                                      uint8_t nexus_msg_len,
                                      uint8_t* out_buf)
{
    uint8_t bytes_serialized = 0;

    // serialize the destination NX ID
    sys_put_be16(dest_id->authority_id, out_buf);
    bytes_serialized += sizeof(uint16_t);
    sys_put_be32(dest_id->device_id, out_buf + bytes_serialized);
    bytes_serialized += sizeof(uint32_t);
    assert(bytes_serialized == (PRODUCT_DATA_LINK_ADDRESS_HEADER_LEN / 2));

    // serialize the source NX ID
    sys_put_be16(src_id->authority_id, out_buf + bytes_serialized);
    bytes_serialized += sizeof(uint16_t);
    sys_put_be32(src_id->device_id, out_buf + bytes_serialized);
    bytes_serialized += sizeof(uint32_t);
    assert(bytes_serialized == PRODUCT_DATA_LINK_ADDRESS_HEADER_LEN);

    // copy Nexus message - already serialized
    memcpy(out_buf + PRODUCT_DATA_LINK_ADDRESS_HEADER_LEN,
           nexus_msg,
           nexus_msg_len);
    bytes_serialized += nexus_msg_len;

    assert(bytes_serialized ==
           (nexus_msg_len + PRODUCT_DATA_LINK_ADDRESS_HEADER_LEN));
    return bytes_serialized;
}

bool product_data_link_send(const struct nx_id* dest_id,
                            const struct nx_id* src_id,
                            const uint8_t* message,
                            uint8_t len)
{
    uint8_t send_buf[PRODUCT_DATA_LINK_ADDRESS_HEADER_LEN +
                     PRODUCT_DATA_LINK_MAX_MESSAGE_SIZE];

    if (len > PRODUCT_DATA_LINK_MAX_MESSAGE_SIZE)
    {
        return false;
    }

    const uint8_t send_buf_len = _product_data_link_serialize_src_dest(
        dest_id, src_id, message, len, send_buf);
    assert(send_buf_len <= (PRODUCT_DATA_LINK_ADDRESS_HEADER_LEN +
                            PRODUCT_DATA_LINK_MAX_MESSAGE_SIZE));
    return phy_uart_send(send_buf, send_buf_len);
}

bool product_link_layer_send_broadcast_request(const struct nx_id* dest_id,
                                               const struct nx_id* src_id,
                                               const uint8_t* message,
                                               uint8_t len)
{
    // phy_uart has no special behavior required for broadcast -there is only
    // one other device electrically at any given time
    return product_data_link_send(dest_id, src_id, message, len);
}
