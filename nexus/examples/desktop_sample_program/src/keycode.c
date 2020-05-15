/** \file keycode.c
 * \brief Implementation of special keycode functions.
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "nx_channel.h"
#include "nxp_keycode.h"
#include <string.h>

// Note: To reduce stack size during execution, rather than handling the
// passthrough keycode synchronously (as is done in this example), it may be
// preferable to copy the `passthrough_keycode` to a static buffer, immediately
// return 'no error' from this function, and process the `passthrough_keycode`
// in the main processing loop.
enum nxp_keycode_passthrough_error nxp_keycode_passthrough_keycode(
    const struct nx_keycode_complete_code* passthrough_keycode)
{
    enum nxp_keycode_passthrough_error error_code =
        NXP_KEYCODE_PASSTHROUGH_ERROR_DATA_UNRECOGNIZED;

    // The first digit of the passthrough code is a 'subtype ID', determining
    // what the purpose of this passthrough code is. Currently, Nexus Channel
    // or manufacturer/device specific commands are supported.

    // passthrough keycode contains ASCII keys, get the int representation
    // of the subtype ID and use it.
    const uint8_t subtype_id = passthrough_keycode->keys[0] - 0x30;
    const uint8_t length = passthrough_keycode->length - 1;
    switch (subtype_id)
    {
        case NXP_KEYCODE_PASSTHROUGH_APPLICATION_SUBTYPE_ID_NX_CHANNEL_ORIGIN_COMMAND:
            // pass the origin command to Nexus Channel for further processing
            // (create a Nexus Channel link, delete a Nexus Channel link, etc)
            // The response code from `handle_origin_command` is ignored
            // here, but it can be useful in debugging scenarios to determine
            // if the origin command did not successfully attempt to create
            // a link.
            nx_channel_handle_origin_command(
                NX_CHANNEL_ORIGIN_COMMAND_BEARER_TYPE_ASCII_DIGITS,
                (void*) &passthrough_keycode->keys[1],
                length);
            error_code = NXP_KEYCODE_PASSTHROUGH_ERROR_NONE;
            break;

        case NXP_KEYCODE_PASSTHROUGH_APPLICATION_SUBTYPE_ID_PROD_ASCII_KEY:
        // product/device specific data contained in the keycode
        /* intentional fallthrough */

        default:
            // not supported
            break;
    }
    return error_code;
}
