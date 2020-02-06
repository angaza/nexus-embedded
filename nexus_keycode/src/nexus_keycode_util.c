/** \file
 * Nexus Keycode Utility Module (Implementation)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "src/nexus_keycode_util.h"

const struct nx_check_key NEXUS_INTEGRITY_CHECK_FIXED_00_KEY = {{0x00,
                                                                 0x00,
                                                                 0x00,
                                                                 0x00,
                                                                 0x00,
                                                                 0x00,
                                                                 0x00,
                                                                 0x00,
                                                                 0x00,
                                                                 0x00,
                                                                 0x00,
                                                                 0x00,
                                                                 0x00,
                                                                 0x00,
                                                                 0x00,
                                                                 0x00}};

const struct nx_check_key NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY = {{0xFF,
                                                                 0xFF,
                                                                 0xFF,
                                                                 0xFF,
                                                                 0xFF,
                                                                 0xFF,
                                                                 0xFF,
                                                                 0xFF,
                                                                 0xFF,
                                                                 0xFF,
                                                                 0xFF,
                                                                 0xFF,
                                                                 0xFF,
                                                                 0xFF,
                                                                 0xFF,
                                                                 0xFF}};

struct nexus_check_value nexus_check_compute(const struct nx_check_key* key,
                                             const void* data,
                                             uint16_t data_size)
{
    struct nexus_check_value value;

    siphash24_compute(value.bytes, data, data_size, key->bytes);

    return value;
}

void nexus_check_compute_pseudorandom_bytes(const struct nx_check_key* key,
                                            const void* seed,
                                            uint16_t seed_size,
                                            void* output,
                                            uint16_t output_size)
{
    // prepare seed data
    // WARNING: Larger than 5 bytes here (4 bytes of data) is not supported
    uint8_t seed_bytes[5] = {0x00}; // one byte for iteration count, then data

    NEXUS_ASSERT(seed_size <= sizeof(seed_bytes) - 1, "unsupported seed size");

    (void) memcpy(&seed_bytes[1], seed, seed_size);

    // compute pseudorandom bytes
    const struct nexus_check_value chunk =
        nexus_check_compute(key, seed_bytes, (uint16_t)(seed_size + 1));

    NEXUS_ASSERT(output_size <= sizeof(chunk.bytes), "unsupported output size");

    (void) memcpy(output, chunk.bytes, output_size);
}

//
// BITSTREAM
//

void nexus_bitstream_init(struct nexus_bitstream* stream,
                          void* bytes,
                          uint16_t capacity,
                          uint16_t length)
{
    NEXUS_ASSERT(capacity >= length, "stream length exceeds capacity");

    stream->data = bytes;
    stream->capacity = capacity;
    stream->length = length;
    stream->position = 0;
}

static void nexus_bitstream_push_bit(struct nexus_bitstream* stream,
                                     bool pushed)
{
    NEXUS_ASSERT(stream->position < stream->capacity,
                 "attempt to overflow bitstream");

    const uint16_t byte_position = stream->position >> 3; // divide by 8
    const uint8_t shift =
        (uint8_t)((byte_position + 1) * 8 - stream->position - 1);
    const uint8_t byte = stream->data[byte_position];

    stream->data[byte_position] =
        (uint8_t)((((byte >> shift) & 0xfe) | (uint8_t) pushed) << shift);

    ++stream->position;

    if (stream->position > stream->length)
    {
        NEXUS_ASSERT(stream->position == stream->length + 1,
                     "stream position invariant failed");

        ++stream->length;
    }
}

void nexus_bitstream_push_uint8(struct nexus_bitstream* stream,
                                uint8_t pushed,
                                uint8_t bits)
{
    NEXUS_ASSERT(bits <= 8, "more than 8 bits pushed from uint8");

    for (uint8_t i = 0; i < bits; ++i)
    {
        const uint8_t bit = (pushed >> (bits - 1)) & 0x01;

        nexus_bitstream_push_bit(stream, bit);

        // Avoid <<= here to avoid false positive with GCC conversion warning.
        const uint8_t pushed_lshift = (uint8_t)(pushed << 1);
        pushed = pushed_lshift;
    }
}

static bool nexus_bitstream_pull_bit(struct nexus_bitstream* stream)
{
    NEXUS_ASSERT(stream->position < stream->length,
                 "attempt to overflow bitstream");

    const uint16_t byte_position = stream->position >> 3; // divide by 8
    const uint8_t shift =
        (uint8_t)((byte_position + 1) * 8 - stream->position - 1);
    const uint8_t byte = stream->data[byte_position];

    ++stream->position;

    return (byte >> shift) & 0x01;
}

uint8_t nexus_bitstream_pull_uint8(struct nexus_bitstream* stream, uint8_t bits)
{
    NEXUS_ASSERT(bits <= 8, "more than 8 bits pulled from uint8");

    uint8_t pulled = 0x00;

    for (uint8_t i = 0; i < bits; ++i)
    {
        pulled = (uint8_t)((pulled << 1) |
                           (uint8_t) nexus_bitstream_pull_bit(stream));
    }

    return pulled;
}

uint16_t nexus_bitstream_pull_uint16_be(struct nexus_bitstream* stream,
                                        uint16_t bits)
{
    NEXUS_ASSERT(bits <= 16, "more than 8 bits pushed from uint8");

    uint8_t msbyte_bits = (uint8_t)(bits > 8 ? 8 : bits);
    uint8_t lsbyte_bits = (uint8_t)(bits > 8 ? bits - 8 : 0);
    uint16_t pulled = 0x0000;

    pulled = nexus_bitstream_pull_uint8(stream, msbyte_bits);

    // Avoid <<= here to avoid false positive with GCC conversion warning.
    const uint16_t pulled_lshift = (uint16_t)(pulled << lsbyte_bits);
    pulled = pulled_lshift;
    pulled |= (uint16_t) nexus_bitstream_pull_uint8(stream, lsbyte_bits);

    return pulled;
}

//
// DIGIT STREAM
//

static uint32_t _chars_to_uint32(const char* chars, const uint8_t count)
{
    // interpret characters as ASCII digits
    uint32_t value = 0;

    for (uint8_t i = 0; i < count; ++i)
    {
        NEXUS_ASSERT('0' <= chars[i] && chars[i] <= '9',
                     "char not an ASCII digit");

        value = value * 10 + (uint32_t)(chars[i] - '0');
    }

    return value;
}

void nexus_digits_init(struct nexus_digits* digits,
                       const char* chars,
                       uint16_t length)
{
    digits->chars = chars;
    digits->length = length;
    digits->position = 0;
}

uint32_t nexus_digits_pull_uint32(struct nexus_digits* digits,
                                  const uint8_t count)
{
    NEXUS_ASSERT(digits->position + count <= digits->length,
                 "too many digits pulled");

    const uint32_t value =
        _chars_to_uint32(digits->chars + digits->position, count);

    // Avoid += here to avoid false positive with GCC conversion warning.
    digits->position = (uint16_t)(count + digits->position);

    return value;
}

uint32_t nexus_digits_try_pull_uint32(struct nexus_digits* digits,
                                      uint8_t count,
                                      bool* underrun)
{
    if (*underrun || nexus_digits_remaining(digits) < count)
    {
        *underrun = true;

        return UINT32_MAX;
    }
    else
    {
        return nexus_digits_pull_uint32(digits, count);
    }
}

//
// BITSET
//

struct _bitset_indices
{
    uint8_t byte_index;
    uint8_t bit_index;
};

static struct _bitset_indices
_bitset_get_indices(const struct nexus_bitset* bitset, uint16_t element)
{
    NEXUS_ASSERT(element < ((uint16_t) bitset->bytes_count) * 8,
                 "element does not fit in bitset");

    // Explicitly note that we do not use bitset unless asserting as above.
    // This has no impact at runtime, and is only a compiler hint.
    (void) bitset;

    struct _bitset_indices indices;

    indices.byte_index = (uint8_t)(element >> 3);
    indices.bit_index = (uint8_t)(element - indices.byte_index * 8);

    return indices;
}

void nexus_bitset_init(struct nexus_bitset* bitset,
                       uint8_t* bytes,
                       uint8_t bytes_count)
{
    bitset->bytes = bytes;
    bitset->bytes_count = bytes_count;
}

void nexus_bitset_add(struct nexus_bitset* bitset, uint16_t element)
{
    const struct _bitset_indices indices = _bitset_get_indices(bitset, element);

    bitset->bytes[indices.byte_index] |= (uint8_t)(0x01 << indices.bit_index);
}

void nexus_bitset_remove(struct nexus_bitset* bitset, uint16_t element)
{
    const struct _bitset_indices indices = _bitset_get_indices(bitset, element);

    bitset->bytes[indices.byte_index] &=
        (uint8_t)(~(uint8_t)(0x01 << indices.bit_index));
}

bool nexus_bitset_contains(const struct nexus_bitset* bitset, uint16_t element)
{
    const struct _bitset_indices indices = _bitset_get_indices(bitset, element);

    return bitset->bytes[indices.byte_index] & (0x01 << indices.bit_index);
}
