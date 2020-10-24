/** \file
 * Nexus Security Module (Header)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#ifndef NEXUS__SRC__NEXUS_SECURITY_H_
#define NEXUS__SRC__NEXUS_SECURITY_H_

#include "src/internal_common_config.h"

#if NEXUS_CHANNEL_LINK_SECURITY_ENABLED

    #ifdef __cplusplus
extern "C" {
    #endif

/** Arbitrary, non-secret key used as seed in key derivation operations.
 *
 * Generated from true random data at random.org.
 *
 * Identical/known for all Nexus Channel devices.
 * *Never* used as an encryption or authentication key!
 */
extern const struct nx_common_check_key
    NEXUS_CHANNEL_PUBLIC_KEY_DERIVATION_KEY_1;

/** Arbitrary, non-secret key used as seed in key derivation operations.
 *
 * Generated from true random data at random.org
 *
 * Identical/known for all Nexus Channel devices.
 * *Never* used as an encryption or authentication key!
 */
extern const struct nx_common_check_key
    NEXUS_CHANNEL_PUBLIC_KEY_DERIVATION_KEY_2;

/* Securely erase a section of memory (RAM).
 *
 * Ensures that the compiler will not optimize away a call to clear memory.
 * This is important in cases where the compiler may detect that an array
 * is no longer used, and eliminate a `memset` call to that array, leaving
 * sensitive data in RAM or on the stack.
 *
 * See:
 * https://www.cryptologie.net/article/419/zeroing-memory-compiler-optimizations-and-memset_s/
 * https://wiki.sei.cmu.edu/confluence/display/c/MSC06-C.+Beware+of+compiler+optimizations
 *
 * \param pointer first byte of memory to clear
 * \param size_data number of valid bytes at location `pointer`
 * \param size_to_erase number of bytes to erase starting at `pointer`
 * \return pointer to last byte erased
 */
void* nexus_secure_memclr(void* pointer,
                          size_t size_data,
                          size_t size_to_erase);

    #ifdef __cplusplus
}
    #endif

#endif // NEXUS_CHANNEL_LINK_SECURITY_ENABLED

#endif /* ifndef NEXUS__SRC__NEXUS_SECURITY_H_ */
