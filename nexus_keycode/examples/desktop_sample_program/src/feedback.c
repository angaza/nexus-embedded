/** \file feedback.c
 * \brief Implementation of Nexus Keycode keycode entry functions.
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "nexus_keycode_port.h"
#include <stdio.h>

bool port_feedback_start(enum port_feedback_type feedback_type)
{
    switch (feedback_type)
    {
        case PORT_FEEDBACK_TYPE_MESSAGE_INVALID:
            printf("\tKeycode is invalid.\n");
            break;
        case PORT_FEEDBACK_TYPE_MESSAGE_APPLIED:
            printf("\tKeycode is valid.\n");
            break;
        case PORT_FEEDBACK_TYPE_MESSAGE_VALID:
            printf("\tKeycode is valid; but, is either a duplicate or had no "
                   "effect.\n");
            break;
        case PORT_FEEDBACK_TYPE_KEY_REJECTED:
#if NEXUS_KEYCODE_PROTOCOL == NEXUS_KEYCODE_PROTOCOL_FULL
            printf("\tInvalid key entry. Full keycodes must be entered without "
                   "spaces and in the form of *(0-9)#.\n");
#endif
#if NEXUS_KEYCODE_PROTOCOL == NEXUS_KEYCODE_PROTOCOL_SMALL
            printf("\tInvalid key entry. Small keycodes must be entered "
                   "without spaces and in the form of 1-5.\n");
#endif
            break;
        case PORT_FEEDBACK_TYPE_DISPLAY_SERIAL_ID:;
            printf("\tSerial ID is %u.\n", port_identity_get_serial_id());
            break;
        case PORT_FEEDBACK_TYPE_KEY_ACCEPTED:
        case PORT_FEEDBACK_TYPE_NONE:
        case PORT_FEEDBACK_TYPE_RESERVED:
        default:
            return false;
    }

    return true;
}
