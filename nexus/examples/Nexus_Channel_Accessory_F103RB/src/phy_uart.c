/** \file phy_uart.c
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
#include "phy_uart.h"
#include "nx_channel.h"
#include <stdbool.h>
#include <stdint.h>
// Include USART for sending/receiving raw data for demo.
#include <drivers/uart.h>

// Zephyr logging for easier demonstration purposes
#include <logging/log.h>
#include <zephyr.h>
LOG_MODULE_REGISTER(phy_uart);

// The devicetree node identifier for the 'nxc_usart' alias, defined
// in the zephyr/stm32f103rb.overlay file
#define NXC_UART_NODE DT_ALIAS(nxc_usart)

// Confirm the USART node is 'okay' (ready to use)
#if DT_NODE_HAS_STATUS(NXC_UART_NODE, okay)
    // "node labels" are used by the `device_get_binding` API
    #define NXC_UART_NODE_LABEL DT_LABEL(NXC_UART_NODE)
#else
    #error "Unsupported board; nxc_usart devicetree alias is not defined"
    #define NXC_UART_NODE_LABEL ""
#endif

// Internal, used to buffer inbound/outbound UART data
static struct
{
    const struct device* nxc_uart_dev;
    uint8_t rx_buf[PHY_UART_RX_TX_BUF_SIZE];
    uint8_t tx_buf[PHY_UART_RX_TX_BUF_SIZE];
    // volatile, as modified inside ISR
    volatile uint8_t rx_buf_len;
    volatile uint8_t tx_buf_len;
    volatile uint8_t tx_bytes_sent;
    volatile bool pending_tx;
    phy_uart_rx_data_handler rx_handler;

    // Times out to indicate idle on RX line
    struct k_timer rx_completed_timer;
} _this;

// "Receive data" from the network-specific logic (LIN, UART, BLE, I2C, etc)
// After receiving, the data will be passed to the `product_data_link` module
void _phy_uart_handle_rx_timer_expiration(struct k_timer* timer_id)
{
    // disable RX interrupt, ignore RX until message passed to next layer
    uart_irq_rx_disable(_this.nxc_uart_dev);
    k_timer_stop(&_this.rx_completed_timer);

    LOG_INF("Read %d bytes", _this.rx_buf_len);

    _this.rx_handler((uint8_t*) _this.rx_buf, (uint8_t) _this.rx_buf_len);
    _this.rx_buf_len = 0;

    // message passed to next layer, re-enable RX
    uart_irq_rx_enable(_this.nxc_uart_dev);
}

static void _nxc_uart_isr(const struct device* dev, void* user_data)
{
    ARG_UNUSED(user_data);

    if (!uart_irq_is_pending(dev))
    {
        LOG_DBG("UART ISR entered, but no IRQ pending");
        return;
    }

    // "This function should be called the
    // first thing in the ISR. Calling uart_irq_rx_ready(),
    // uart_irq_tx_ready(), uart_irq_tx_complete()
    // allowed only after this."
    uart_irq_update(dev);

    // UART RX buffer has at least one char
    if (uart_irq_rx_ready(dev))
    {
        _this.rx_buf_len +=
            uart_fifo_read(_this.nxc_uart_dev,
                           (uint8_t*) (_this.rx_buf + _this.rx_buf_len),
                           (uint8_t)(sizeof(_this.rx_buf) - _this.rx_buf_len));
        // If no more data is received for 100ms, go to timer completion handler
        k_timer_start(&_this.rx_completed_timer,
                      K_MSEC(PHY_UART_RX_TIMEOUT_MILLISECONDS),
                      K_MSEC(PHY_UART_RX_TIMEOUT_MILLISECONDS));
    }

    // UART TX buffer can accept a new char
    if (uart_irq_tx_ready(dev))
    {
        const uint8_t bytes_to_send =
            (uint8_t) _this.tx_buf_len - _this.tx_bytes_sent;
        if (bytes_to_send > 0)
        {
            // offset the first byte to send by the number of bytes sent already
            _this.tx_bytes_sent +=
                uart_fifo_fill(_this.nxc_uart_dev,
                               (uint8_t*) (_this.tx_buf + _this.tx_bytes_sent),
                               bytes_to_send);
        }
        else
        {
            // No more data to send, disable TX ready interrupt
            uart_irq_tx_disable(_this.nxc_uart_dev);
            _this.pending_tx = false;
            _this.tx_bytes_sent = 0;
            _this.tx_buf_len = 0;
            LOG_INF("TX Buffer empty");
        }
    }
}

// See: https://docs.zephyrproject.org/latest/reference/peripherals/uart.html
void phy_uart_init(const phy_uart_rx_data_handler uart_rx_handler)
{
    _this.rx_buf_len = 0;
    _this.tx_buf_len = 0;
    _this.tx_bytes_sent = 0;
    _this.pending_tx = false;
    _this.rx_handler = uart_rx_handler;

    // Used to detect when incoming data on RX is 'completed'
    k_timer_init(
        &_this.rx_completed_timer, _phy_uart_handle_rx_timer_expiration, NULL);

    _this.nxc_uart_dev = device_get_binding(NXC_UART_NODE_LABEL);
    if (!_this.nxc_uart_dev)
    {
        // should not occur, cannot initialize UART
        assert(false);
    }
    uart_irq_rx_disable(_this.nxc_uart_dev);
    uart_irq_tx_disable(_this.nxc_uart_dev);

    uart_irq_callback_user_data_set(_this.nxc_uart_dev, _nxc_uart_isr, NULL);

    // TX IRQ will be enabled within `phy_uart_send`
    uart_irq_rx_enable(_this.nxc_uart_dev);
}

bool phy_uart_send(const uint8_t* data, uint8_t len)
{
    if (len > sizeof(_this.tx_buf))
    {
        // Cannot fit the outbound message in the outbound UART buffer
        return false;
    }

    if (_this.pending_tx)
    {
        // already have a buffered outbound message, cannot send another
        return false;
    }

    // Note: In a 'real' implementation, the source and destination NXID need
    // to be transmitted as well, in another layer that provides addressing.
    // In this implementation, a simple header serialization/deserialization
    // provides this in the `product_data_link`.
    _this.pending_tx = true;
    memcpy((uint8_t*) _this.tx_buf, data, len);
    _this.tx_buf_len = len;

    // includes 12-byte 'header' from `product_data_link.c` at this point
    LOG_INF("Buffered %d bytes to send", _this.tx_buf_len);
    uart_irq_tx_enable(_this.nxc_uart_dev);
    return true;
}
