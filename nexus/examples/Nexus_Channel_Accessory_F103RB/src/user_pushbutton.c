/** \file user_pushbutton.h
 * \brief A user pushbutton demonstration
 * \author Angaza
 * \copyright 2021 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 *
 * This module configures a single pushbutton on the target board to
 * allow for a Nexus Channel "Accessory Reset" functionality.
 */
#include "user_pushbutton.h"
#include <stdbool.h>
#include <stdint.h>

// For accessory 'factory reset' function
#include "nx_channel.h"

// GPIO configuration drivers
#include <drivers/gpio.h>

#include "flash_filesystem.h"

// Zephyr logging for easier demonstration purposes
#include <logging/log.h>
#include <zephyr.h>
#include <assert.h>

LOG_MODULE_REGISTER(user_pushbutton);

#define USER_BUTTON_NODE DT_ALIAS(sw0)

// Confirm the pushbutton node is 'okay' (ready to use)
#if DT_NODE_HAS_STATUS(USER_BUTTON_NODE, okay)
    // "node labels" are used by the `device_get_binding` API
    #define USER_BUTTON_GPIO_LABEL DT_GPIO_LABEL(USER_BUTTON_NODE, gpios)
    #define USER_BUTTON_GPIO_PIN DT_GPIO_PIN(USER_BUTTON_NODE, gpios)
    #define USER_BUTTON_GPIO_FLAGS (GPIO_INPUT | DT_GPIO_FLAGS(USER_BUTTON_NODE, gpios))
#else
    #error "Unsupported board; user pushbutton alias sw0 not defined"
    #define USER_BUTTON_GPIO_LABEL ""
    #define USER_BUTTON_GPIO_PIN 0
    #define USER_BUTTON_GPIO_FLAGS 0
#endif

// Internal, used to buffer inbound/outbound pushbutton data
static struct
{
    const struct device* user_pb_dev;
    struct gpio_callback button_cb_data;
    // volatile, as modified inside ISR
    volatile uint8_t seconds_elapsed;
    // times how long the user pushbutton is held
    struct k_timer user_pb_timer;
    // Used to perform deferred work (not in an interrupt)
    struct k_work user_pb_work;
} _this;

// interrupt handler - fires when button is pressed
void user_pushbutton_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    // read once more, confirm logic high, then start timer
    if (gpio_pin_get(_this.user_pb_dev, USER_BUTTON_GPIO_PIN))
    {
        LOG_INF("Button pressed at %" PRIu32 ", starting timer\n", k_cycle_get_32());
        k_timer_start(&_this.user_pb_timer,
                    K_SECONDS(USER_PUSHBUTTON_HOLD_TO_RESET_SECONDS),
                    K_SECONDS(USER_PUSHBUTTON_HOLD_TO_RESET_SECONDS));
    }
}

void _user_pb_erase_channel_links_and_nexus_nv(struct k_work *item)
{
        LOG_INF("Resetting Nexus Channel accessory link state!");
        nx_channel_accessory_delete_all_links();

        // Below line for *ease of demo use only*. Here, we also erase all
        // Nexus NV, so that the controller can reuse the same 'link' origin
        // command/keycode to establish a link again. In reality, we would
        // generate a new keycode (origin commands cannot be reused).
        flash_filesystem_erase_nexus_nv();
}

// Handle pushbutton timer expiration by resetting the accessory link state
void _user_pb_handle_timer_expiration(struct k_timer* timer_id)
{
    k_timer_stop(&_this.user_pb_timer);

    // check current GPIO level - if it is still high, consider it a 'long press'
    if (gpio_pin_get(_this.user_pb_dev, USER_BUTTON_GPIO_PIN))
    {
        k_work_submit(&_this.user_pb_work);
    }
}

void user_pushbutton_init(void)
{
    _this.user_pb_dev = device_get_binding(USER_BUTTON_GPIO_LABEL);
    assert(_this.user_pb_dev != NULL);

    int ret = gpio_pin_configure(_this.user_pb_dev, USER_BUTTON_GPIO_PIN, USER_BUTTON_GPIO_FLAGS);
    assert(ret == 0);

    // configure GPIO interrupt to be triggered on pin state change to logical '1'
    ret = gpio_pin_interrupt_configure(_this.user_pb_dev, USER_BUTTON_GPIO_PIN, GPIO_INT_EDGE_TO_ACTIVE);
    assert(ret == 0);

    // configure function to call when the button is pressed
    gpio_init_callback(&_this.button_cb_data, user_pushbutton_pressed, BIT(USER_BUTTON_GPIO_PIN));
    gpio_add_callback(_this.user_pb_dev, &_this.button_cb_data);

    // Used to detect when pushbutton has been held down for a specified duration
    k_timer_init(
        &_this.user_pb_timer, _user_pb_handle_timer_expiration, NULL);

    // Used to defer NV erase to non-interrupt context
    k_work_init(&_this.user_pb_work, _user_pb_erase_channel_links_and_nexus_nv);

    LOG_INF("Set up button at %s pin %d\n", USER_BUTTON_GPIO_LABEL, USER_BUTTON_GPIO_PIN);
}
