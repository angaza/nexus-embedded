/** \file crc_ccitt.h
 * \brief CRC-CCITT integrity checking algorithm used by Nexus internally.
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 *
 * By default, the implementation of this header is included within
 * `common/crc_ccitt.h`. The header is provided at this 'top level' so that
 * product-side code may also use the same CRC functionality (if required),
 * and so that the CRC functionality is usable by other Nexus modules
 * (such as Nexus Channel) without duplicating code.
 */

#ifndef __NEXUS__COMMON__INC__CRC_CCITT_H_
#define __NEXUS__COMMON__INC__CRC_CCITT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the 16-bit CRC CCITT value for an arbitrary
 * length of bytes.  Assumptions:
 *
 * CRC Polynomial = 0x1021
 * Initial CRC Value = 0xffff
 * Final XOR value = 0
 *
 * Sample Input Data:
 * {0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39} (1-9 in ASCII)
 * Sample Output CRC: 0x29B1
 */
uint16_t compute_crc_ccitt(void* bytes, uint8_t bytes_length);

#ifdef __cplusplus
}
#endif

#endif
