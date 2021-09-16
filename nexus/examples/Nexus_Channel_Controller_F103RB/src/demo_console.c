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
#include "nxp_common.h"
// to report current PAYG credit of this device
#include "product_payg_state_manager.h"

// to handle command for erasing Nexus NV/flash
#include "flash_filesystem.h"

// Zephyr logging for easier demonstration purposes
#include <logging/log.h>
#include <zephyr.h>
LOG_MODULE_REGISTER(demo_console);

// Max input command size
#define MAX_CONSOLE_MESSAGE_IN_SIZE 32

static unsigned char _demo_console_input_buffer[MAX_CONSOLE_MESSAGE_IN_SIZE];
static bool _initialized = false;

static bool _handle_keycode_entry(char* cmd_string)
{
    bool handled = false;
    // keycodes are the only valid command beginning with "*"
    if (cmd_string[0] == '*')
    {
        // Act as a client, and make a request to the server hosting the
        // 'battery' resource on this same device.
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
        // Act as a client, and make a request to the server hosting the
        // 'battery' resource on this same device.
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

static bool _handle_erase_flash_nv(char* cmd_string)
{
    bool handled = false;
    if (strncmp(cmd_string, "erasenv", 7) == 0)
    {
        // first, clear Nexus system state in RAM
        nx_common_shutdown();
        // then, erase flash
        if ((flash_filesystem_erase_nexus_nv() == 0))
        {
            product_payg_state_manager_set_credit(0);
            LOG_INF("Erased Nexus NV/flash, reset PAYG credit to 0");
        }
        else
        {
            LOG_INF("Error erasing Nexus NV/flash");
        }
        // reinitiaize Nexus Channel Core with uptime (in seconds)
        // approximately divide by 1000 (1024).
        nx_common_init((uint32_t)(k_uptime_get() >> 10));
        // request Nexus thread to execute
        nxp_common_request_processing();

        handled = true;
    }
    return handled;
}

// Internal function which waits for a semaphore indicating that there is
// user input to process, then processes it.
static void _demo_console_process_user_input(char* cmd_string)
{
    LOG_INF("Processing input\n");
    bool command_handled = false;

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

    if (!command_handled)
    {
        // check testing command to erase product and Nexus NV ('erasenv')
        command_handled = _handle_erase_flash_nv(cmd_string);
    }

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
