/** \file
 * Nexus Keycode Utility Module (Header)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#ifndef __NEXUS__KEYCODE__SRC__NEXUS_KEYCODE_UTIL_H_
#define __NEXUS__KEYCODE__SRC__NEXUS_KEYCODE_UTIL_H_

#include "src/internal_keycode_config.h"

// Included for memset, memcmp, memcpy
#include <string.h>
// Included to confirm bit widths (char == 8 bits)
#include <limits.h>

#ifndef CHAR_BIT
#error "Cannot confirm number of bits in a byte is 8."
#endif
#if CHAR_BIT != 8
#error                                                                         \
    "Number of bits in a byte is not reporting 8 - bitshift operations may be invalid!"
#endif

#define U8TO64_LE(p)                                                           \
    (((uint64_t)((p)[0])) | ((uint64_t)((p)[1]) << 8) |                        \
     ((uint64_t)((p)[2]) << 16) | ((uint64_t)((p)[3]) << 24) |                 \
     ((uint64_t)((p)[4]) << 32) | ((uint64_t)((p)[5]) << 40) |                 \
     ((uint64_t)((p)[6]) << 48) | ((uint64_t)((p)[7]) << 56))

/** Used for internal integrity checks.
 */
extern const struct nx_core_check_key NEXUS_INTEGRITY_CHECK_FIXED_00_KEY;

/** Used for internal integrity checks
 */
extern const struct nx_core_check_key NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY;

/** Result of an internal authentication check computation.
 */
NEXUS_PACKED_STRUCT nexus_check_value
{
    uint8_t bytes[8];
};

struct nexus_check_value nexus_check_compute(
    const struct nx_core_check_key* key, const void* data, uint16_t data_size);

/** Compute pseudorandom bytes based on a seed and secret key.
 *
 * Warning: This implementation only supports seeds of 4 bytes or fewer
 * and output sizes of 8 bytes or fewer; it is intended to be compatible
 * with approaches that work with larger input and output sizes (as in
 * the generic Python implementation) but that support isn't yet
 * necessary in firmware, and so is not implemented here.
 *
 * \param key to target the result for this specific unit
 * \param seed input seed used to compute pseudorandom bytes
 * \param seed_size number of bytes in seed
 * \param output pointer to location to store pseudorandom bytes output
 * \param output_size max number of bytes to copy to output
 */
void nexus_check_compute_pseudorandom_bytes(const struct nx_core_check_key* key,
                                            const void* seed,
                                            uint16_t seed_size,
                                            void* output,
                                            uint16_t output_size);

static inline uint64_t
nexus_check_value_as_uint64(const struct nexus_check_value* value)
{
    return U8TO64_LE(value->bytes);
}

// endianness routines
static inline uint16_t nexus_endian_htobe16(uint16_t host)
{

    return (uint16_t)((host >> CHAR_BIT) | (host << CHAR_BIT));
}

static inline uint16_t nexus_endian_be16toh(const uint16_t big_endian)
{
    const uint8_t* bytes = (uint8_t*) &big_endian;

    return (uint16_t)((((uint16_t)(bytes[0])) << CHAR_BIT) | bytes[1]);
}

static inline uint32_t u32min(uint32_t lhs, uint32_t rhs)
{
    return lhs < rhs ? lhs : rhs;
}

// bitstream
struct nexus_bitstream
{
    uint8_t* data;
    uint16_t capacity; // in bits
    uint16_t length; // in bits
    uint16_t position; // in bits
};

void nexus_bitstream_init(struct nexus_bitstream* bitstream,
                          void* bytes,
                          uint16_t capacity,
                          uint16_t length);

static inline uint16_t
nexus_bitstream_length_in_bits(const struct nexus_bitstream* stream)
{
    return stream->length;
}

/*@ignore@*/
/* SPLINT to ignore this; only used in testing (not in production code) */
static inline const uint8_t*
nexus_bitstream_data(const struct nexus_bitstream* stream)
{
    return stream->data;
}
/*@end@*/

static inline void
nexus_bitstream_set_bit_position(struct nexus_bitstream* stream,
                                 uint16_t position)
{
    NEXUS_ASSERT(position <= stream->length, "position out of range");

    stream->position = position;
}

void nexus_bitstream_push_uint8(struct nexus_bitstream* bitstream,
                                uint8_t data,
                                uint8_t bits);

uint8_t nexus_bitstream_pull_uint8(struct nexus_bitstream* bitstream,
                                   uint8_t bits);

uint16_t nexus_bitstream_pull_uint16_be(struct nexus_bitstream* bitstream,
                                        uint16_t bits);

// digit stream
struct nexus_digits
{
    const char* chars;
    uint16_t length; // in digits
    uint16_t position; // in digits
};

void nexus_digits_init(struct nexus_digits* digits,
                       const char* chars,
                       uint16_t length);

// Inlined to save space
static inline uint16_t
nexus_digits_length_in_digits(const struct nexus_digits* digits)
{
    return digits->length;
}

// Inlined to save space
static inline uint16_t nexus_digits_position(const struct nexus_digits* digits)
{
    return digits->position;
}

// Inlined to save space
static inline uint16_t nexus_digits_remaining(const struct nexus_digits* digits)
{
    return (uint16_t)(digits->length - digits->position);
}

uint32_t nexus_digits_pull_uint32(struct nexus_digits* digits, uint8_t count);
uint32_t nexus_digits_try_pull_uint32(struct nexus_digits* digits,
                                      uint8_t count,
                                      bool* underrun);

// Inlined to save space
static inline uint8_t nexus_digits_pull_uint8(struct nexus_digits* digits,
                                              uint8_t count)
{
    return (uint8_t) nexus_digits_pull_uint32(digits, count);
}

// Inlined to save space
static inline uint16_t nexus_digits_pull_uint16(struct nexus_digits* digits,
                                                uint8_t count)
{
    return (uint16_t) nexus_digits_pull_uint32(digits, count);
}

// bitset
struct nexus_bitset
{
    uint8_t* bytes;
    uint8_t bytes_count; // in bytes
};

void nexus_bitset_init(struct nexus_bitset* bitset,
                       uint8_t* bytes,
                       const uint8_t bytes_count);

void nexus_bitset_add(struct nexus_bitset* bitset, uint16_t element);
void nexus_bitset_remove(struct nexus_bitset* bitset, uint16_t element);
bool nexus_bitset_contains(const struct nexus_bitset* bitset, uint16_t element);

#ifndef __FRAMAC__
static inline void nexus_bitset_clear(struct nexus_bitset* const bitset)
{
    memset(bitset->bytes, 0x00, bitset->bytes_count);
}
#endif

#endif
