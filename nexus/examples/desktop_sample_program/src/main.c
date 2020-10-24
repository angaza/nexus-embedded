/** \file main.c
 * \brief Sample program main entry point and execution loop.
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clock.h"
#include "identity.h"
#include "keyboard.h"
#include "menu.h"
#include "nonvol.h"
#include "nx_common.h"
#include "nx_keycode.h"
#include "nxp_common.h"
#include "payg_state.h"
#include "processing.h"
#include "screen.h"

// Nexus Channel example product-specific resource
#include "nexus_batt_resource.h"

// Capture keyboard 'ctrl-C' interrupts.
void sigint_handler(/*int sig*/)
{
    processing_deinit();
    putchar('\n');
    exit(0);
}

int main(void)
{
    // Catch program exit, so we can handle wrap-up operations if needed.
    signal(SIGINT, (__sighandler_t) sigint_handler);

    printf("Initializing product interfaces...\n");
    clock_init();
    nv_init();
    keyboard_init();
    identity_init();
    processing_init();
    payg_state_init();
    printf("Done with product interfaces\n");

    printf("Initializing Nexus library...\n");
    // Pass in current system uptime to initialize Nexus timekeeping
    nx_common_init(clock_read_monotonic_time_seconds());

    // Initialize custom resources after `nx_common_init`
    battery_resource_init();
    printf("Done\n");

    // Start the main loop.
    while (1)
    {
        processing_execute();
        screen_display_status();

        // menu will block until user selects action
        menu_prompt();
        clock_consume_credit();

        // processing
        keyboard_process_keycode();
    }
}
