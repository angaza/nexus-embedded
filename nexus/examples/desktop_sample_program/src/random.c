/** \file random.c
 * \brief A mock implementation of a random number generator.
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include <stdlib.h>
#include <time.h>

#include "nxp_channel.h"

uint32_t nxp_channel_random_value(void)
{
    // return (uint32_t) rand();
    // For consistent demonstration, use a fixed random number
    // so that salt generation for link keys are identical.
    // Uncomment to run program with random salt (as in production)
    return 12345678;
}
