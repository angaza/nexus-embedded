/** \file siphash_24.h
 * \brief Siphash 2-4 algorithm used by Nexus internally.
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 *
 * By default, the implementation of this header is included within
 * `common/siphash_24.c`. The header is provided at this 'top level' so that
 * product-side code may also use the same Siphash functionality (if required),
 * and so that the Siphash functionality is usable by other Nexus modules
 * (such as Nexus Channel) without duplicating code.
 */

#ifndef __NEXUS__COMMON__INC__SIPHASH_24_H_
#define __NEXUS__COMMON__INC__SIPHASH_24_H_

#include <stdint.h>

/** Compute a Siphash 2-4 hash result over input bytes.
 *
 * Given a key and an input stream of bytes, compute the Siphash 2-4 64-bit
 * (8 byte) result.
 *
 * `out` must be able to contain at least 8 bytes.
 * `in` may be any length of bytes.
 *
 * \param out pointer to output where the Siphash 2-4 result will be placed.
 * \param in pointer to input bytes to compute hash over
 * \param inlen length of input bytes to compute from `in`
 * \param key 128-bit (16 byte) secret key used to compute hash result
 * \return void
 */
void siphash24_compute(uint8_t* out,
                       const uint8_t* in,
                       const uint32_t inlen,
                       const uint8_t* key);

#endif
