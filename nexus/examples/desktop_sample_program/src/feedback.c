/** \file feedback.c
 * \brief Implementation of Nexus Keycode keycode entry functions.
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "nxp_keycode.h"
#include <stdio.h>

bool nxp_keycode_feedback_start(enum nxp_keycode_feedback_type feedback_type)
{
    switch (feedback_type)
    {
        case NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_INVALID:
            printf("\tKeycode is invalid.\n");
            break;
        case NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_APPLIED:
            printf("\tKeycode is valid.\n");
            break;
        case NXP_KEYCODE_FEEDBACK_TYPE_MESSAGE_VALID:
            printf("\tKeycode is valid; but, is either a duplicate or had no "
                   "effect.\n");
            break;
        case NXP_KEYCODE_FEEDBACK_TYPE_KEY_REJECTED:
#if CONFIG_NEXUS_KEYCODE_USE_FULL_KEYCODE_PROTOCOL
            printf("\tInvalid key entry. Full keycodes must be entered without "
                   "spaces and in the form of *(0-9)#.\n");
#elif CONFIG_NEXUS_KEYCODE_USE_SMALL_KEYCODE_PROTOCOL
            printf("\tInvalid key entry. Small keycodes must be entered "
                   "without spaces and in the form of 1-5.\n");
#else
#error "Error: Keycode protocol configuration missing..."
#endif
            break;
        case NXP_KEYCODE_FEEDBACK_TYPE_DISPLAY_SERIAL_ID:;
            printf("\tSerial ID is %u.\n", nxp_keycode_get_user_facing_id());
            break;
        case NXP_KEYCODE_FEEDBACK_TYPE_KEY_ACCEPTED:
        case NXP_KEYCODE_FEEDBACK_TYPE_NONE:
        case NXP_KEYCODE_FEEDBACK_TYPE_RESERVED:
        default:
            return false;
    }

    return true;
}
