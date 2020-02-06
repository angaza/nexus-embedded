/** \file screen.c
 * \brief Trivial implementation of user feedback of device PAYG state.
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "screen.h"
#include "nexus_keycode_port.h"
#include "payg_state.h"
#include <stdio.h>

void screen_display_status(void)
{
    if (port_payg_state_get_current() == PAYG_STATE_UNLOCKED)
    {
        printf("The device is unlocked\n");
    }
    else
    {
        printf("Credit remaining: %d seconds\n",
               payg_state_get_remaining_credit());
    }
}
