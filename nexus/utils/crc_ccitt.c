/* \file crc_ccitt.c
 * \brief CRC-CCITT Reference C implementation
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
*/

#include "crc_ccitt.h"

uint16_t _crc_ccitt_update(const uint8_t* byte, uint16_t cur_crc)
{
    uint32_t crc_new =
        (uint32_t)((unsigned char) (cur_crc >> 8) | (cur_crc << 8));
    crc_new ^= *byte;
    crc_new ^= (unsigned char) (crc_new & 0xff) >> 4;
    crc_new ^= crc_new << 12;
    crc_new ^= (crc_new & 0xff) << 5;
    return (uint16_t) crc_new;
}

uint16_t compute_crc_ccitt(void* bytes, uint8_t bytes_length)
{
    uint16_t crc_ccitt = 0xffff;

    uint8_t* byte_ptr = (uint8_t*) bytes;
    while (bytes_length--)
    {
        crc_ccitt = _crc_ccitt_update(byte_ptr++, crc_ccitt);
    }

    return crc_ccitt;
}
