/** \file
 * Nexus Internal Utility Module (Header)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#ifndef NEXUS__SRC__NEXUS_UTIL_H_
#define NEXUS__SRC__NEXUS_UTIL_H_

#include "src/internal_common_config.h"

// Included for memset, memcmp, memcpy
#include <string.h>
// Included to confirm bit widths (char == 8 bits)
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CHAR_BIT
    #error "Cannot confirm number of bits in a byte is 8."
#endif
#if CHAR_BIT != 8
    #error                                                                     \
        "Number of bits in a byte is not reporting 8 - bitshift operations may be invalid!"
#endif

#define U8TO64_LE(p)                                                           \
    (((uint64_t)((p)[0])) | ((uint64_t)((p)[1]) << 8) |                        \
     ((uint64_t)((p)[2]) << 16) | ((uint64_t)((p)[3]) << 24) |                 \
     ((uint64_t)((p)[4]) << 32) | ((uint64_t)((p)[5]) << 40) |                 \
     ((uint64_t)((p)[6]) << 48) | ((uint64_t)((p)[7]) << 56))

#define NEXUS_UTIL_MAX_WINDOW_BITSET_SIZE_BYTES 5

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

// endianness routines. Internal use.
static inline uint16_t nexus_endian_htobe16(uint16_t host)
{
    return (uint16_t)((host >> CHAR_BIT) | (host << CHAR_BIT));
}

static inline uint32_t nexus_endian_htobe32(uint32_t host)
{
    // Can be replaced by `return host` on a big endian host platform
    uint32_t result = 0;
    ((unsigned char*) &result)[0] = (unsigned char) (host >> 24);
    ((unsigned char*) &result)[1] = (unsigned char) ((host & 0x00FF0000) >> 16);
    ((unsigned char*) &result)[2] = (unsigned char) ((host & 0x0000FF00) >> 8);
    ((unsigned char*) &result)[3] = (unsigned char) (host & 0xFF);
    return result;
}

static inline uint16_t nexus_endian_be16toh(const uint16_t big_endian)
{
    const uint8_t* bytes = (uint8_t*) &big_endian;
    return (uint16_t)((((uint16_t)(bytes[0])) << CHAR_BIT) | bytes[1]);
}

static inline uint16_t nexus_endian_htole16(const uint16_t host)
{
    // Can be replaced by `return host` on a little endian host platform
    uint16_t result = 0;
    ((unsigned char*) &result)[0] = (unsigned char) (host & 0xFF);
    ((unsigned char*) &result)[1] = (unsigned char) ((host & 0x0000FF00) >> 8);
    return result;
}

static inline uint32_t nexus_endian_htole32(const uint32_t host)
{
    // Can be replaced by `return host` on a little endian host platform
    uint32_t result = 0;
    ((unsigned char*) &result)[0] = (unsigned char) (host & 0xFF);
    ((unsigned char*) &result)[1] = (unsigned char) ((host & 0x0000FF00) >> 8);
    ((unsigned char*) &result)[2] = (unsigned char) ((host & 0x00FF0000) >> 16);
    ((unsigned char*) &result)[3] = (unsigned char) (host >> 24);
    return result;
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

// Returns UINT8_MAX if not enough digits remain or pulled value too large
uint8_t nexus_digits_pull_uint8(struct nexus_digits* digits, uint8_t count);

// Returns UINT16_MAX if not enough digits remain or pulled value too large
uint16_t nexus_digits_pull_uint16(struct nexus_digits* digits, uint8_t count);

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

// Used for manipulating bitset 'windows with a center', as used by
// `nexus_keycode_pro` and `nexus_channel_om`.

/* A 'window with a center'.
 *
 * Typically used for storing 'received IDs' within the context of keycodes.
 *
 * Any IDs in the window below the center are marked as 'received' with a
 * flag 'bit'. The window 'moves' by receiving an ID above the center, which
 * shifts the window to the right.
 */
struct nexus_window
{
    uint32_t center_index; // 'center' of the window
    struct nexus_bitset flags; // actual 'flags' set for this window
    uint8_t flags_below; // total number of bits/flags in `flags`
    uint8_t flags_above; // how far ahead of center index to recognize?
};

/*! \brief Convenience function for initializing a window.
 *
 * Given an uninitialized window struct, take an array representing bitflags
 * *below and including* the window center, the integer value of the
 * window center, and window size (left/right). Then, initialize the window
 * to ease setting/getting bitflags within the window.
 *
 * \param window window to initialize
 * \param flag_array pointer to bitflags representing center ID and flags
 * 'below'
 * \param flag_array_bytes number of bytes used by `flag_array`
 * \param center_index rightmost flag value in `flag_array` (7, 15, 23, 31, etc)
 * \param flags_below total number of flags to the 'left' of the center_index,
 * including center_index
 * \param flags_above flags to the 'right' of center_index, excluding
 * center_index
 */
void nexus_util_window_init(
    struct nexus_window* window,
    uint8_t* flag_array, // first element
    const uint8_t flag_array_bytes,
    const uint32_t center_index, // 'largest' bitflag in flag_array
    uint8_t flags_below,
    uint8_t flags_above);

/*! \brief Determine if an ID is within a window.
 *
 * Returns true if the ID is within the window, false otherwise.
 * Does not indicate whether the ID is set or not - will return true
 * for both 'set' and 'unset' IDs.
 *
 * \param id id to check
 * \param window window to check against
 * \return True if ID is located within this window, false otherwise.
 */
bool nexus_util_window_id_within_window(const struct nexus_window* window,
                                        const uint32_t id);

/*! \brief Determine if an ID is already set inside an ID window.
 *
 * Searches the window for the ID, and if it is found as already received
 * and set within the window, returns true. Does not determine if the ID
 * value 'falls within' the window
 *
 * \param id id to check
 * \param window window to check against
 * \return True if ID is within this window and already set, false otherwise.
 */
bool nexus_util_window_id_flag_already_set(const struct nexus_window* window,
                                           const uint32_t id);

/*! \brief Set the appropriate ID flag within a Nexus ID window.
 *
 *  ID must actually be a valid ID within the window; or this function
 *  will fail silently (and leave the window unmodified). This function
 *  is idempotent - the resulting `window` is identical if the ID is already
 *  set in the mask or if the ID was not previously set.
 *
 * \param window window to set flag within
 * \param id id to set
 * \return true if ID was set successfully, false otherwise
 */
bool nexus_util_window_set_id_flag(struct nexus_window* window,
                                   const uint32_t id);

#ifndef __FRAMAC__
static inline void nexus_bitset_clear(struct nexus_bitset* const bitset)
{
    memset(bitset->bytes, 0x00, bitset->bytes_count);
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* ifndef NEXUS__SRC__NEXUS_UTIL_H_ */
