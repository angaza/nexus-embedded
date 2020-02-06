/** \file keycode.c
 * \brief Implementation of special keycode functions.
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "nexus_keycode_port.h"

enum port_passthrough_error port_passthrough_keycode(
    const struct nx_keycode_complete_code* passthrough_keycode)
{
    (void) passthrough_keycode;
    return PORT_PASSTHROUGH_ERROR_DATA_UNRECOGNIZED;
}
