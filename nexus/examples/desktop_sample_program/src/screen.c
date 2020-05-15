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
#include "nx_channel.h"
#include "nxp_core.h"
#include "nxp_keycode.h"
#include "payg_state.h"
#include <stdio.h>

void screen_display_status(void)
{
    if (nxp_core_payg_state_get_current() == NXP_CORE_PAYG_STATE_UNLOCKED)
    {
        printf("The device is unlocked\n");
    }
    else
    {
        printf("Credit remaining: %d seconds\n",
               payg_state_get_remaining_credit());
    }
}

void screen_display_nexus_channel_state(void)
{
    printf("Number of active Nexus Channel links: %d", nx_channel_link_count());
}