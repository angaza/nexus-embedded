/** \file keycode.c
 * \brief Implementation of special keycode functions.
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "nxp_keycode.h"

enum nxp_keycode_passthrough_error nxp_keycode_passthrough_keycode(
    const struct nx_keycode_complete_code* passthrough_keycode)
{
    (void) passthrough_keycode;
    return NXP_KEYCODE_PASSTHROUGH_ERROR_DATA_UNRECOGNIZED;
}
