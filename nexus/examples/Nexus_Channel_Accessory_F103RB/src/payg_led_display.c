/** \file payg_led_display.h
 * \brief Visualize PAYG state with onboard LED
 * \author Angaza
 * \copyright 2021 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 *
 * Basic demonstration to show PAYG state on the onboard LED of
 * the STM32F103RB Nucleo-64 dev board (LD2).
 */
#include "payg_led_display.h"
#include <stdbool.h>
#include <stdint.h>

// GPIO configuration drivers
#include <drivers/gpio.h>

// Zephyr logging for easier demonstration purposes
#include <logging/log.h>
#include <zephyr.h>
#include <assert.h>

LOG_MODULE_REGISTER(payg_led);

#define PAYG_LED_NODE DT_ALIAS(payg_led)

// Confirm the pushbutton node is 'okay' (ready to use)
#if DT_NODE_HAS_STATUS(PAYG_LED_NODE, okay)
    // "node labels" are used by the `device_get_binding` API
    #define HAS_LED 1
    #define PAYG_LED_LABEL DT_GPIO_LABEL(PAYG_LED_NODE, gpios)
    #define PAYG_LED_GPIO_PIN DT_GPIO_PIN(PAYG_LED_NODE, gpios)
    #define PAYG_LED_GPIO_FLAGS (GPIO_OUTPUT_ACTIVE | DT_GPIO_FLAGS(PAYG_LED_NODE, gpios))
#else
    #error "Unsupported board; user led alias 'payg-led' not defined"
    #define PAYG_LED_LABEL ""
    #define PAYG_LED_GPIO_PIN 0
    #define PAYG_LED_GPIO_FLAGS 0
#endif

// Internal, used to store LED state
static struct
{
    const struct device* payg_led_dev;
    struct k_delayed_work payg_led_work;
    volatile bool led_is_on;
    volatile bool should_blink;
} _this;

static void payg_led_display_blink_timeout(struct k_work *work)
{
    if (!_this.should_blink)
    {
        return;
    }
    _this.led_is_on = !_this.led_is_on;
    gpio_pin_set(_this.payg_led_dev, PAYG_LED_GPIO_PIN, (int)_this.led_is_on);
    k_delayed_work_submit(&_this.payg_led_work, PAYG_LED_DISPLAY_BLINK_MSEC);
}

void payg_led_display_init(void)
{
    _this.payg_led_dev = device_get_binding(PAYG_LED_LABEL);
    assert(_this.payg_led_dev != NULL);

    int ret = gpio_pin_configure(_this.payg_led_dev, PAYG_LED_GPIO_PIN, PAYG_LED_GPIO_FLAGS);
    assert(ret == 0);

    // initialize LED to an 'off' state, let other modules control the state
    gpio_pin_set(_this.payg_led_dev, PAYG_LED_GPIO_PIN, false);
    _this.led_is_on = false;
    _this.should_blink = false;

    k_delayed_work_init(&_this.payg_led_work, payg_led_display_blink_timeout);
    LOG_INF("Set up PAYG LED at %s pin %d\n", PAYG_LED_LABEL, PAYG_LED_GPIO_PIN);
}

void payg_led_display_begin_blinking(void)
{
    _this.should_blink = true;
    // immediately submit
    k_delayed_work_submit(&_this.payg_led_work, K_MSEC(0));
}

void payg_led_display_solid_on(void)
{
    _this.should_blink = false;
    gpio_pin_set(_this.payg_led_dev, PAYG_LED_GPIO_PIN, true);
    _this.led_is_on = true;
}

void payg_led_display_solid_off(void)
{
    _this.should_blink = false;
    gpio_pin_set(_this.payg_led_dev, PAYG_LED_GPIO_PIN, false);
    _this.led_is_on = false;
}