/** \file phy_uart.h
 * \brief A demo implementation of networking interface to Nexus Channel
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 *
 * This module configures a duplex UART peripheral for use by
 * `product_link_layer`. This module permits transmitting and receiving
 * raw bytes between connected devices.
 */

#ifndef PHY_UART__H
#define PHY_UART__H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

// Size of UART RX and TX buffers, in bytes
#define PHY_UART_RX_TX_BUF_SIZE 140

// Number of milliseconds to wait for idle on the RX
// line before calling `rx_data_handler` callback with
// received data.
#define PHY_UART_RX_TIMEOUT_MILLISECONDS 100

// Signature of a function to handle UART RX data after it is received
typedef void (*phy_uart_rx_data_handler)(const uint8_t* data, uint8_t len);

/** @brief phy_uart_init
 *
 * Initialize UART peripheral, and register a function to
 * call when data is received on the UART line.
 *
 * @param[in] rx_handler Function to call when data is received
 */
void phy_uart_init(const phy_uart_rx_data_handler uart_rx_handler);

/**
 * @brief phy_uart_send
 *
 * Send raw bytes on the UART bus. Should not be called from
 * an interrupt context (should call from threads only).
 *
 * May return false if another buffered message is not yet sent,
 * or if message to send exceeds available TX buffer size.
 *
 * @param[in] data Pointer to the data to send
 * @param[in] len Number of bytes to be sent
 *
 * @return true if outbound data buffered successfully, else false
 */
bool phy_uart_send(const uint8_t* data, uint8_t len);

#ifdef __cplusplus
}
#endif
#endif // PHY_UART__H