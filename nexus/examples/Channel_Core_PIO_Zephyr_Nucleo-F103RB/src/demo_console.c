/** \file demo_console.c
 * \brief An interactive console for demonstration purposes
 * \author Angaza
 * \copyright 2021 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "demo_console.h"

#include <console/console.h>

#include "nx_channel.h"
#include "nx_common.h"
#include "nx_keycode.h"
#include "nxp_channel.h"
// to report current PAYG credit of this device
#include "product_payg_state_manager.h"

// Zephyr logging for easier demonstration purposes
#include <logging/log.h>
#include <zephyr.h>
LOG_MODULE_REGISTER(demo_console);

// Max input command size
#define MAX_CONSOLE_MESSAGE_IN_SIZE 32

static unsigned char _demo_console_input_buffer[MAX_CONSOLE_MESSAGE_IN_SIZE];
static bool _initialized = false;

#ifdef CHANNEL_CORE_SUPPORTED_DEMO_BUILD_ENABLED
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

static bool _handle_get_batt_command(char* cmd_string)
{
    bool handled = false;
    // if user input is `get`, attempt get request
    if (strncmp(cmd_string, "get", 3) == 0 ||
        strncmp(cmd_string, "GET", 3) == 0)
    {
        // Act as a client, and make a request to the server hosting the
        // 'battery' resource on this same device.
        LOG_INF("Making GET request to 'batt' resource");
        struct nx_id this_device_nexus_id = nxp_channel_get_nexus_id();
        nx_channel_do_get_request("batt",
                                  &this_device_nexus_id,
                                  NULL,
                                  &get_battery_response_handler,
                                  NULL);
        handled = true;
    }
    return handled;
}

static bool _handle_post_batt_command(char* cmd_string)
{
    bool handled = false;
    if (strncmp(cmd_string, "post", 4) == 0 ||
        strncmp(cmd_string, "POST", 4) == 0)
    {
        struct nx_id this_device_nexus_id = nxp_channel_get_nexus_id();
        nx_channel_init_post_request("batt",
                                     &this_device_nexus_id,
                                     NULL,
                                     &post_battery_response_handler,
                                     NULL);

        if (strncmp(cmd_string + 4, "20", 2) == 0)
        {
            oc_rep_begin_root_object();
            oc_rep_set_uint(root, th, 20); // set key 'th' to 20
            oc_rep_end_root_object();
            nx_channel_do_post_request();
            handled = true;
        }
        else if (strncmp(cmd_string + 4, "35", 2) == 0)
        {
            oc_rep_begin_root_object();
            oc_rep_set_uint(root, th, 35); // set key 'th' to 35
            oc_rep_end_root_object();
            nx_channel_do_post_request();
            handled = true;
        }
        else
        {
            LOG_INF("Ignoring user input. Valid POST options are 'post20' "
                    "or 'post35'\n");
        }
    }
    return handled;
}
#endif // CHANNEL_CORE_SUPPORTED_DEMO_BUILD_ENABLED

#ifdef KEYCODE_SUPPORTED_DEMO_BUILD_ENABLED
static bool _handle_keycode_entry(char* cmd_string)
{
    bool handled = false;
    // keycodes are the only valid command beginning with "*"
    if (cmd_string[0] == '*')
    {
        LOG_INF("Handling keycode");
        struct nx_keycode_complete_code keycode;
        keycode.keys = cmd_string;
        keycode.length = strnlen(cmd_string, MAX_CONSOLE_MESSAGE_IN_SIZE);

        const bool keycode_processed =
            nx_keycode_handle_complete_keycode(&keycode);
        if (!keycode_processed)
        {
            LOG_INF("Problem processing keycode");
        }
        handled = true;
    }
    return handled;
}

static bool _handle_check_payg_credit(char* cmd_string)
{
    bool handled = false;
    if (strncmp(cmd_string, "pc", 2) == 0)
    {
        const uint32_t current_payg_credit =
            product_payg_state_manager_get_current_credit();
        if (current_payg_credit <
            PRODUCT_PAYG_STATE_MANAGER_UNLOCKED_CREDIT_SENTINEL)
        {
            LOG_INF("PAYG credit remaining=%d seconds", current_payg_credit);
        }
        else
        {
            LOG_INF("PAYG credit *unlocked*!");
        }
        handled = true;
    }
    return handled;
}
#endif // KEYCODE_SUPPORTED_DEMO_BUILD_ENABLED

// Internal function taking a command string from the user, and
// processing it.
static void _demo_console_process_user_input(char* cmd_string)
{
    LOG_INF("Processing input\n");
    bool command_handled = false;

#ifdef CHANNEL_CORE_SUPPORTED_DEMO_BUILD_ENABLED
    // get (batt resource)
    command_handled = _handle_get_batt_command(cmd_string);
    if (!command_handled)
    {
        // post20, post35 (batt resource)
        command_handled = _handle_post_batt_command(cmd_string);
    }
#endif // CHANNEL_CORE_SUPPORTED_DEMO_BUILD_ENABLED

#ifdef KEYCODE_SUPPORTED_DEMO_BUILD_ENABLED
    if (!command_handled)
    {
        // keycode (*XXXX....#)
        command_handled = _handle_keycode_entry(cmd_string);
    }

    if (!command_handled)
    {
        // check current PAYG credit (`pc`)
        command_handled = _handle_check_payg_credit(cmd_string);
    }
#endif // KEYCODE_SUPPORTED_DEMO_BUILD_ENABLED

    if (!command_handled)
    {
        LOG_INF("Command not recognized");
    }
}

void demo_console_wait_for_user_input(void)
{
    if (!_initialized)
    {
        console_getline_init();
        _initialized = true;
    }

    // XXX wait for pending log statements to flush here
    printk("demo> ");
    // blocks waiting for input. Will only work on single-line ASCII string
    // input
    char* in_cmd = console_getline();
    memset(
        _demo_console_input_buffer, 0x00, sizeof(_demo_console_input_buffer));
    memcpy(_demo_console_input_buffer,
           in_cmd,
           strnlen(in_cmd, sizeof(_demo_console_input_buffer)));

    // Process user input
    _demo_console_process_user_input(_demo_console_input_buffer);
}
