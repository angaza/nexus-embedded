/** \file identity.c
 * \brief A mock implementation of one way to store device identity.
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "identity.h"
#include "nonvol.h"
#include "nx_keycode.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static NEXUS_PACKED_STRUCT identity_struct _this;

uint32_t port_identity_get_serial_id(void)
{
    return _this.serial_id;
}

struct nx_check_key port_identity_get_secret_key(void)
{
    return _this.secret_key;
}

void identity_init(void)
{
    _this.serial_id = 0;
    memset(_this.secret_key.bytes, 0, sizeof(_this.secret_key.bytes));

    // attempt to read from NV
    bool valid_nv =
        prod_nv_read_identity(sizeof(struct identity_struct), (void*) &_this);

    // If we have a valid identity in NV, don't attempt to write a new one.
    if (valid_nv)
    {
        return;
    }

    printf("Please enter an integer serial ID.\n");
    if (scanf("%u", (uint32_t*) &_this.serial_id) != 1)
    {
        printf("Unable to parse the serial ID.\n");
        exit(1);
    }

    printf("Please enter the 16-byte hexidecimal secret key. For example, "
           "\"deadbeef1020304004030201feebdaed\".\n");
    for (size_t count = 0; count < sizeof(_this.secret_key.bytes) /
                                       sizeof(*_this.secret_key.bytes);
         count++)
    {
        if (scanf("%2hhx", &_this.secret_key.bytes[count]) != 1)
        {
            printf("Unable to parse the secret key.\n");
            exit(1);
        }
    }
    // If read failed, write our results from above to NV
    prod_nv_write_identity(sizeof(struct identity_struct), (void*) &_this);
}
