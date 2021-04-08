/** \file
 * Nexus Internal Utility Module (Implementation)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */
#include "src/nexus_util.h"
#include "src/internal_common_config.h" // to get siphash/crc

const struct nx_common_check_key NEXUS_INTEGRITY_CHECK_FIXED_00_KEY = {{0x00,
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

const struct nx_common_check_key NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY = {{0xFF,
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

struct nexus_check_value nexus_check_compute(
    const struct nx_common_check_key* key, const void* data, uint16_t data_size)
{
    struct nexus_check_value value;

    siphash24_compute(
        value.bytes, (const uint8_t*) data, data_size, key->bytes);

    return value;
}

void nexus_check_compute_pseudorandom_bytes(
    const struct nx_common_check_key* key,
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

    stream->data = (uint8_t*) bytes;
    stream->capacity = capacity;
    stream->length = length;
    stream->position = 0;
}

void nexus_bitstream_push_bit(struct nexus_bitstream* stream, bool pushed)
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
        NEXUS_ASSERT(stream->capacity >= stream->length,
                     "stream length exceeds capacity");
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

bool nexus_bitstream_pull_bit(struct nexus_bitstream* stream)
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

uint8_t nexus_digits_pull_uint8(struct nexus_digits* digits, uint8_t count)
{
    if (nexus_digits_remaining(digits) < count)
    {
        return UINT8_MAX;
    }
    const uint32_t result = nexus_digits_pull_uint32(digits, count);

    if (result > UINT8_MAX)
    {
        NEXUS_ASSERT_FAIL_IN_DEBUG_ONLY(result < UINT8_MAX,
                                        "Invalid digits for uint8");
        return UINT8_MAX;
    }
    return (uint8_t) result;
}

uint16_t nexus_digits_pull_uint16(struct nexus_digits* digits, uint8_t count)
{
    if (nexus_digits_remaining(digits) < count)
    {
        return UINT16_MAX;
    }
    const uint32_t result = nexus_digits_pull_uint32(digits, count);

    if (result > UINT16_MAX)
    {
        NEXUS_ASSERT_FAIL_IN_DEBUG_ONLY(result < UINT8_MAX,
                                        "Invalid digits for uint16");
        return UINT16_MAX;
    }
    return (uint16_t) result;
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

void nexus_util_window_init(struct nexus_window* window,
                            uint8_t* flag_array, // first element
                            const uint8_t flag_array_bytes,
                            const uint32_t center_index,
                            uint8_t flags_below,
                            uint8_t flags_above)
{
    NEXUS_ASSERT(flag_array_bytes * CHAR_BIT == flags_below + 1,
                 "Invalid window flags_below");
    NEXUS_ASSERT(flag_array_bytes <= NEXUS_UTIL_MAX_WINDOW_BITSET_SIZE_BYTES,
                 "Window of this size not supported");

    window->center_index = center_index;
    window->flags_below = flags_below;
    window->flags_above = flags_above;

    // create bitset from flag array bytes. Note that flag_array must remain
    // in scope for the window created from it to be meaningful..
    nexus_bitset_init(&window->flags, flag_array, flag_array_bytes);
}

bool nexus_util_window_id_within_window(const struct nexus_window* window,
                                        const uint32_t id)
{
    const uint32_t window_min = window->center_index - window->flags_below;
    const uint32_t window_max = window->center_index + window->flags_above;

    NEXUS_ASSERT(window_min < window_max, "Invalid window!");

    return id >= window_min && id <= window_max;
}

static bool
_nexus_util_window_mask_idx_from_id(const struct nexus_window* window,
                                    const uint32_t id,
                                    uint8_t* mask_id_index)
{
    if (!nexus_util_window_id_within_window(window, id))
    {
// avoid false positive where analyzer indicates further lines
// are not hit (unit tests confirm they are)
#ifndef __clang_analyzer__
        return false;
#endif
    }

    const uint32_t center_idx = window->center_index;

    if (center_idx >= id)
    {
        // ID is below center index
        *mask_id_index = (uint8_t)(window->flags_below - (center_idx - id));
    }
    else
    {
        // ID is above center index
        *mask_id_index = (uint8_t)(window->flags_below + (id - center_idx));
    }

    return true;
}

bool nexus_util_window_id_flag_already_set(const struct nexus_window* window,
                                           const uint32_t id)
{

    if ((id > window->center_index) ||
        !nexus_util_window_id_within_window(window, id))
    {
// clang indicates that subsequent statements after this if are not reachable,
// yet unit tests show that they are.
#ifndef __clang_analyzer__
        return false;
#endif
    }

    // ID falls into the range of our current window, is it set?
    uint8_t mask_id_index = 0;
    const bool mask_id_valid =
        _nexus_util_window_mask_idx_from_id(window, id, &mask_id_index);

    NEXUS_ASSERT(mask_id_valid, "Mask ID invalid after checking");

    // found index, is this index set in our current window?
    return mask_id_valid &&
           nexus_bitset_contains(&window->flags, mask_id_index);
}

bool nexus_util_window_set_id_flag(struct nexus_window* window,
                                   const uint32_t id)
{
    const uint32_t old_center = window->center_index;
    if (id > (old_center + window->flags_above))
    {
        return false;
    }
    else if (id < (old_center - window->flags_below))
    {
        return false;
    }

    // ID falls into the range of our current window, is it set?
    uint8_t mask_id_index = 0;

    if (!_nexus_util_window_mask_idx_from_id(window, id, &mask_id_index))
    {
        NEXUS_ASSERT(0, "should not reach here");
        // Can't find the ID in the window; return false and don't set
        // should be caught by the checks above, but be safe.
        return false;
    }
    // center index is the rightmost ID in the stored window, so if
    // the new ID equals the center index, and it isn't set, just set it.
    if (id <= window->center_index)
    {
        nexus_bitset_add(&window->flags, mask_id_index);
        return true;
    }

    // --- BELOW HERE, MOVING THE WINDOW TO THE RIGHT ---

    // how many flags/bits to shift right by
    const uint32_t center_increment = id - old_center;
    NEXUS_ASSERT(center_increment > 0,
                 "Attempting to move window by 0, unexpected");
    if (center_increment > window->flags_below)
    {
        // clear the window - we've moved by more than the stored flags
        nexus_bitset_clear(&window->flags);
    }
    else
    {
        // mask array here may be larger than actual window flags byte array
        uint8_t new_mask[NEXUS_UTIL_MAX_WINDOW_BITSET_SIZE_BYTES];
        memset(&new_mask, 0x00, sizeof(new_mask));

        // temporary storage for the new mask while we calculate it
        struct nexus_bitset new_mask_bitset;
        nexus_bitset_init(&new_mask_bitset, &new_mask[0], sizeof(new_mask));
        // <= window->flags below to 'set' the center index value as well -
        // the total number of flags in the window is window->flags_below + 1
        // (1 for the center index)
        for (uint32_t i = center_increment; i <= window->flags_below; i++)
        {
            // copy values from the old mask into the new mask, offset
            // by the change in the center index value
            if (nexus_bitset_contains(&window->flags, (uint16_t) i))
            {
                nexus_bitset_add(&new_mask_bitset,
                                 (uint16_t)(i - center_increment));
            }
        }
        NEXUS_ASSERT(window->flags.bytes_count * 8 == window->flags_below + 1,
                     "flag bytes count does not match number of flag bits");

        // copy 'moved' window values
        (void) memcpy(
            window->flags.bytes, &new_mask, window->flags.bytes_count);
    }

    // finally, update the window center index value to the new ID.
    window->center_index = id;
    // and set its flag
    // (window center is always 'flags_below' from the bottom of window)
    nexus_bitset_add(&window->flags, window->flags_below);

    return true;
}
