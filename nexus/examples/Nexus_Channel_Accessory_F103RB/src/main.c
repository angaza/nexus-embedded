// copied from:
// https://github.com/platformio/platform-ststm32/blob/master/examples/zephyr-blink/src/main.c

/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "oc/include/oc_api.h"
#include "oc/messaging/coap/coap.h"

#include "nxp_channel.h"
#include "nxp_common.h"

// Module to convert Nexus Channel messages to
// 'on the wire' representation.
#include "product_data_link.h"

#include <device.h>
#include <devicetree.h>
#include <drivers/gpio.h>
#include <string.h>
#include <zephyr.h>

// Flash filesystem, specific to this implementation
#include "flash_filesystem.h"
#include "product_nexus_identity.h"
#include "product_payg_state_manager.h"
#include "user_pushbutton.h"
#include "payg_led_display.h"

// Zephyr logging for easier demonstration purposes
#include <logging/log.h>
LOG_MODULE_REGISTER(main);

static void _assign_nexus_payg_identities(void)
{
    // Store testing values to flash for:
    // Nexus ID / PAYG ID
    // Keycode Secret Key
    // Channel Secret Key
    // NOTE: In production, these should vary from device to device!
    const struct nx_id TEST_NEXUS_ID = {
        0xFFFF, // authority ID 0xFFFF for 'testing'
        0x0700AAAA, // arbitrary device ID 117484202 (0x0700AAAA)
    };
    // A fake key just used for demo purposes...
    const struct nx_common_check_key TEST_NEXUS_COMMON_DEMO_SECRET_KEY = {
        {0xAA,
         0xBB,
         0xCC,
         0xDD,
         0xEE,
         0xFF,
         0x11,
         0x22,
         0x33,
         0x44,
         0x55,
         0x66,
         0x77,
         0x88,
         0x99,
         0x00}};
    product_nexus_identity_set_nexus_id(&TEST_NEXUS_ID);
    product_nexus_identity_set_nexus_channel_secret_key(
        &TEST_NEXUS_COMMON_DEMO_SECRET_KEY);
}

void main(void)
{
    // Wait 1s for the UART to initialize
    k_busy_wait(1 * 1000 * 1000); // us

    // Initialize flash filesystem/NV
    if (!flash_filesystem_init())
    {
        // should never reach here
        assert(false);
        return;
    }
    // In a real device, this would not happen in every boot in main.c, but
    // would happen once in the factory during provisioning
    _assign_nexus_payg_identities();

    // Initialize PAYG LED display hardware
    payg_led_display_init();

    // Initialize internal PAYG state management
    product_payg_state_manager_init();

    // Prepare data link, and use
    // `nx_channel_network_receive` to process received
    // messages.
    product_data_link_init(&nx_channel_network_receive);

    user_pushbutton_init();

    LOG_INF("---Nexus Embedded Demonstration Started (ACCESSORY)---");

    // the main thread does no further work in the accessory device,
    // there is no interactive demo console for input
    return;
}
