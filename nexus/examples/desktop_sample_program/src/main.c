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
#include "nonvol.h"
#include "nx_core.h"
#include "nx_keycode.h"
#include "nxp_core.h"
#include "payg_state.h"
#include "processing.h"
#include "screen.h"

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
    signal(SIGINT, sigint_handler);

    printf("Initializing product interfaces...\n");
    clock_init();
    nv_init();
    keyboard_init();
    identity_init();
    processing_init();
    payg_state_init();
    printf("Done with product interfaces\n");

    printf("Initializing Nexus Keycode library...\n");
    nx_core_init();
    printf("Done\n");

    // Start the main loop.
    while (1)
    {
        processing_execute();
        screen_display_status();
        keyboard_prompt_keycode(stdin);
        clock_consume_credit();
        keyboard_process_keycode();
    }
}
