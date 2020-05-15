/** \file menu.c
 * \brief Menu selection for example program
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include <stdio.h>
#include <string.h>

#include "menu.h"
// include 'nx_core' to get core, including configuration values
#include "keyboard.h"
#include "network.h"
#include "nexus_batt_resource.h"
#include "nx_core.h"
#include "processing.h"
#include "screen.h"

void _display_menu_options(void)
{
    printf("\n\n--------\n");
    printf("\n--Menu--\n");
    printf("\n--------\n\n");
    printf("1. Enter Nexus Keycode\n");
    printf("2. Display Nexus Channel Status\n");
    printf("3. Enter processing loop (hide menu)\n");
    printf("4. Simulate GET to Battery Resource\n");
    printf("5. Update Battery Resource (Low Battery Threshold)'\n");
    printf("Selection: ");
}

#define MENU_EXIT_VALUE 1000
void menu_prompt(void)
{
    bool exit_menu = false;
    int selection = -1;
    char cbuf;
    do
    {
        // clear out any pending input
        while ((cbuf = getchar()) != '\n' && cbuf != EOF)
            ; // clear input

        // display menu selection
        _display_menu_options();

        // and then search for a decimal digit
        while (scanf("%d", &selection) == 0 && cbuf != '\n' && cbuf != EOF)
        {
            cbuf = getchar();
        }

        // before proceeding, ensure we've cleared any characters from input
        // stream -- downstream code calls fgets, expecting no newlines
        while (cbuf != '\n' && cbuf != EOF)
        {
            cbuf = getchar();
        }

        switch (selection)
        {
            case 1:
                enable_simulated_accessory_response();
                keyboard_prompt_keycode(stdin);
                exit_menu = true;
                break;
            case 2:
                // TODO display link handshakes in progress
                screen_display_nexus_channel_state();
                break;
            case 3:
                processing_idle_loop(1);
                break;
            case 4:
                disable_simulated_accessory_response();
                battery_resource_simulate_get();
                break;
            case 5:
                disable_simulated_accessory_response();
                keyboard_prompt_update_battery_threshold(stdin);
                break;

            case MENU_EXIT_VALUE:
                printf("Continue (do nothing)");
                break;
            default:
                printf("Unknown selection.\n");
                break;
        }
        if (exit_menu)
        {
            return;
        }
    } while (selection != MENU_EXIT_VALUE);
}
