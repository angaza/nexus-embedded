// copied from:
// https://github.com/platformio/platform-ststm32/blob/master/examples/zephyr-blink/src/main.c

/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "oc/include/oc_api.h"
#include "oc/messaging/coap/coap.h"

#include "network.h"
#include "nxp_channel.h"
#include "nxp_common.h"
#include <console/console.h>
#include <device.h>
#include <devicetree.h>
#include <drivers/gpio.h>
#include <string.h>
#include <zephyr.h>

// Zephyr logging for easier demonstration purposes
#include <logging/log.h>
LOG_MODULE_REGISTER(main);

/* For Zephyr builds on the Nucleo-F103RB, also see board devicetree file at
 * ~/.platformio/packages/framework-zephyr/boards/arm/nucleo_f103rb/nucleo_f103rb.dts
 * which defines the 'led0' alias as GPIOA5 'active high', as well as
 * noting that `zephyr,console` and `zephyr,shell-uart` are redirected to USART2
 * (115200 baud). The 'base' dts files for this board (defining whats missing
 * from the file above) are at:
 * ~/.platformio/packages/framework-zephyr/dts/arm/st/f1/stm32f103Xb.dtsi (flash
 * erase block size, USB peripheral)
 *
 * ~/.platformio/packages/framework-zephyr/dts/arm/st/f1/stm32f1.dtsi (flash
 * layout, RCC/GPIO control register addresses, NVIC, IRQ priorities, and
 * peripheral register addresses, timer control registers, ADC, DMA, etc).
 *
 * In general, Zephyr configures what a given board supports with this set of
 * hierarchical dts files.
 */

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
    #define LED0 DT_GPIO_LABEL(LED0_NODE, gpios)
    #define PIN DT_GPIO_PIN(LED0_NODE, gpios)
    #if DT_PHA_HAS_CELL(LED0_NODE, gpios, flags)
        #define FLAGS DT_GPIO_FLAGS(LED0_NODE, gpios)
    #endif
#else
    /* A build error here means your board isn't set up to blink an LED. */
    #error "Unsupported board: led0 devicetree alias is not defined"
    #define LED0 ""
    #define PIN 0
#endif

#ifndef FLAGS
    #define FLAGS 0
#endif

#define MAX_CONSOLE_MESSAGE_IN_SIZE 64

K_SEM_DEFINE(console_input_sem, 0, 1);
unsigned char console_input_buffer[MAX_CONSOLE_MESSAGE_IN_SIZE];

// valid testing/dev Nexus ID value (authority ID 0xFFFF)
static struct nx_id THIS_DEVICE_NX_ID = {0xFFFF, 0x12345678};

/* Function to handle *responses* to a GET request.
 *
 * Nexus Channel Core will call this function after a response
 * is received to a previously-sent GET request.
 */
void get_battery_response_handler(nx_channel_client_response_t* response)
{
    LOG_INF("[GET Response Handler] Received response with code %d from Nexus "
            "ID [Authority ID 0x%04X, Device ID "
            "0x%08X]",
            response->code,
            response->source->authority_id,
            response->source->device_id);

    // parse the payload
    LOG_INF("[GET Response Handler] Parsing payload");
    oc_rep_t* rep = response->payload;
    while (rep != NULL)
    {
        char* cur_key = oc_string(rep->name);
        LOG_INF("[GET Response Handler] Key %s", cur_key);
        switch (rep->type)
        {
            case OC_REP_BOOL:
                LOG_INF("%d", rep->value.boolean);
                break;

            case OC_REP_INT:
                LOG_INF("%lld", rep->value.integer);
                break;
            default:
                break;
        }
        rep = rep->next;
    }
}

/* Function to handle *responses* to a POST request.
 *
 * Nexus Channel Core will call this function after a response
 * is received to a previously-sent POST request.
 */
void post_battery_response_handler(nx_channel_client_response_t* response)
{
    LOG_INF("[POST Response Handler] Received response with code %d from Nexus "
            "ID [Authority ID 0x%04X, Device ID "
            "0x%08X]",
            response->code,
            response->source->authority_id,
            response->source->device_id);

    // parse the payload
    LOG_INF("[POST Response Handler] Parsing payload");
    oc_rep_t* rep = response->payload;
    while (rep != NULL)
    {
        char* cur_key = oc_string(rep->name);
        LOG_INF("[POST Response Handler] Key %s", cur_key);
        switch (rep->type)
        {
            // battery resource only has one property which can be
            // set via POST - `th`, an integer value.
            // see:
            // https://angaza.github.io/nexus-channel-models/resource_types/core/101-battery/redoc_wrapper.html#/paths/~1batt/post
            case OC_REP_INT:
                LOG_INF("%lld", rep->value.integer);
                break;
            default:
                break;
        }
        rep = rep->next;
    }
}

void main(void)
{
    // allow time for the UART/console to initialize
    k_busy_wait(1 * 1000 * 1000); // us

    LOG_INF("---Nexus Channel Core Demonstration---\n"
            "Valid options are 'get', 'post20', or 'post35'\n");

    struct device* dev;
    int ret = 0;

    dev = device_get_binding(LED0);
    if (dev == NULL)
    {
        return;
    }

    ret = gpio_pin_configure(dev, PIN, GPIO_OUTPUT_ACTIVE | FLAGS);
    if (ret < 0)
    {
        return;
    }

    console_getline_init();

    struct nx_id dummy_id = {55, 1234};

    coap_packet_t request_packet;
    uint8_t buffer[120] = {0};

    uint8_t token = 0;
    uint16_t message_id = 0;

    while (1)
    {
        // Wait for notification of user input, then simulate
        // an appropriate client request based on input (GET or POST)
        k_sem_take(&console_input_sem, K_FOREVER);

        // if user input is `get`, attempt get request
        if (strncmp(console_input_buffer, "get", 3) == 0 ||
            strncmp(console_input_buffer, "GET", 3) == 0)
        {
            // Act as a client, and make a request to the server hosting the
            // 'battery' resource on this same device.
            LOG_INF("Making GET request to 'batt' resource");
            nx_channel_do_get_request("batt",
                                      &THIS_DEVICE_NX_ID,
                                      NULL,
                                      &get_battery_response_handler,
                                      NULL);
        }

        else if (strncmp(console_input_buffer, "post", 4) == 0 ||
                 strncmp(console_input_buffer, "POST", 4) == 0)
        {
            nx_channel_init_post_request("batt",
                                         &THIS_DEVICE_NX_ID,
                                         NULL,
                                         &post_battery_response_handler,
                                         NULL);

            if (strncmp(console_input_buffer + 4, "20", 2) == 0)
            {
                oc_rep_begin_root_object();
                oc_rep_set_uint(root, th, 20); // set key 'th' to 20
                oc_rep_end_root_object();
                nx_channel_do_post_request();
            }
            else if (strncmp(console_input_buffer + 4, "35", 2) == 0)
            {
                oc_rep_begin_root_object();
                oc_rep_set_uint(root, th, 35); // set key 'th' to 35
                oc_rep_end_root_object();
                nx_channel_do_post_request();
            }
            else
            {
                LOG_INF("Ignoring user input. Valid POST options are 'post20' "
                        "or 'post35'\n");
                continue;
            }
        }
        else
        {
            LOG_INF("Ignoring user input. Valid options are 'get', 'post20', "
                    "or 'post35'\n");
        }
    }
}

/** Read input from the 'network' (console, in this case).
 *
 * Insert that input into an 128-byte buffer, give a semaphore
 * to indicate that the data is available to read.
 *
 * Do not attempt to read any more input data until
 * data has been read/processed by another thread.
 */
void get_input_from_user(void)
{
    struct device* dev = device_get_binding(LED0);

    while (1)
    {
        // LED LD2 ON when ready/waiting for input
        gpio_pin_set(dev, PIN, 1);
        while (k_sem_count_get(&console_input_sem) != 0)
        {
            // do nothing, cannot accept more input until
            // existing input is processed
        }

        LOG_INF("Waiting for user input\n");
        // blocks waiting for input
        char* s = console_getline();
        LOG_INF("Received user input\n");

        // XXX cannot be used for CBOR, we terminate input on newline/EOL
        // and strnlen certainly won't work on bytestreams with null contents
        memset(console_input_buffer, 0x00, MAX_CONSOLE_MESSAGE_IN_SIZE);

        memcpy(
            console_input_buffer, s, strnlen(s, MAX_CONSOLE_MESSAGE_IN_SIZE));

        k_sem_give(&console_input_sem);

        gpio_pin_set(dev, PIN, 0);
        // sleep for 250ms to make LED transition visible
        k_msleep(250);
    }
}

// Thread to get console input from user for demo
// (to determine what action to 'simulate')
K_THREAD_DEFINE(get_input_from_user_id,
                512,
                get_input_from_user,
                NULL,
                NULL,
                NULL,
                6,
                0,
                0);

//
// COMMON FUNCTIONS REQUIRED BY NEXUS BELOW
//

uint32_t nxp_channel_random_value(void)
{
    return sys_rand32_get();
}

struct nx_id nxp_channel_get_nexus_id(void)
{
    return THIS_DEVICE_NX_ID;
}