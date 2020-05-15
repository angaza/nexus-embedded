/** \file
 * Nexus Security Module (Implementation)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */
// Pull in secure implementations (_s) of standard lib functions if available
#define __STDC_WANT_LIB_EXT1__ 1

#include "src/nexus_security.h"
// explicitly include string to check for LIB_EXT1
#include <string.h>

// Non-secret, publicly known key used to derive key from input material
const struct nx_core_check_key NEXUS_CHANNEL_PUBLIC_KEY_DERIVATION_KEY_1 = {
    {0x8A,
     0x5E,
     0xE2,
     0xB4,
     0xA0,
     0xCF,
     0xF4,
     0x93,
     0xE5,
     0xED,
     0xA2,
     0xD1,
     0xE4,
     0xC4,
     0x5B,
     0x25}};

const struct nx_core_check_key NEXUS_CHANNEL_PUBLIC_KEY_DERIVATION_KEY_2 = {
    {0xE2,
     0x6F,
     0xDB,
     0x34,
     0xE4,
     0xDD,
     0x40,
     0xBC,
     0x63,
     0x35,
     0xC6,
     0x09,
     0xAA,
     0xDF,
     0xAA,
     0xC4}};

void* nexus_secure_memclr(void* pointer, size_t size_data, size_t size_to_erase)
{
#ifdef __STDC_LIB_EXT1__
    // Use built-in secure erase if available
    const int result = memset_s(pointer, size_data, 0, size_to_remove);
    NEXUS_ASSERT(result == 0, "Unexpected error securely clearing memory.");
#else
    // Use reference implementation
    if (size_to_erase > size_data)
    {
        size_to_erase = size_data;
    }
    volatile unsigned char* p = pointer;
    while (size_to_erase--)
    {
        *p++ = 0;
    }
#endif
    return pointer;
}
