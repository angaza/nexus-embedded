#include "include/common/siphash_24.h"
#include "src/nexus_keycode_util.h"
#include "unity.h"

// Other support libraries
#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/********************************************************
 * DEFINITIONS
 *******************************************************/
/********************************************************
 * PRIVATE TYPES
 *******************************************************/

/********************************************************
 * PRIVATE DATA
 *******************************************************/

static const uint8_t EXAMPLE_INPUTS[] = {
    0x1, 0x5, 0x0, 0xd7, 0x41, 0xd, 0x18, 0x19, 0x6e, 0x1, 0xf7, 0x3};
static const uint8_t EXAMPLE_LENGTHS[] = {1, 4, 1, 8, 7, 5, 5, 5, 8, 1, 8, 3};
static const uint8_t EXAMPLE_BYTES[] = {
    0xab, 0x5e, 0xb, 0x71, 0x96, 0xef, 0xbb};

static struct
{
    struct nexus_bitstream stream;
    uint8_t data[128];
} _this;

/********************************************************
 * PRIVATE FUNCTIONS
 *******************************************************/

// Setup (called before any 'test_*' function is called, automatically)
void setUp(void)
{
}

// Teardown (called after any 'test_*' function is called, automatically)
void tearDown(void)
{
}

void test_nexus_bitstream_length_in_bits__init_with_various__length_is_various(
    void)
{
    // first scenario
    nexus_bitstream_init(&_this.stream, _this.data, 12, 0);

    TEST_ASSERT_EQUAL_UINT(nexus_bitstream_length_in_bits(&_this.stream), 0);

    // second scenario
    nexus_bitstream_init(&_this.stream, _this.data, 18, 18);

    TEST_ASSERT_EQUAL_UINT(nexus_bitstream_length_in_bits(&_this.stream), 18);
}

void test_nexus_bitstream_data__init_with_array__data_is_array(void)
{
    nexus_bitstream_init(&_this.stream, _this.data, 12, 0);
    TEST_ASSERT_EQUAL_UINT_ARRAY(nexus_bitstream_data(&_this.stream),
                                 _this.data,
                                 sizeof(struct nexus_bitstream));
}

void test_nexus_bitstream_push_uint8__trivial_byte_pushed__array_matches_expected(
    void)
{
    const uint8_t byte = 0x42;

    nexus_bitstream_init(&_this.stream, _this.data, sizeof(_this.data) * 8, 0);
    nexus_bitstream_push_uint8(&_this.stream, byte, 8);

    TEST_ASSERT_EQUAL_UINT(nexus_bitstream_length_in_bits(&_this.stream), 8);
    TEST_ASSERT_EQUAL_UINT(_this.data[0], byte);
}

void test_nexus_bitstream_push_uint8__trivial_bits_pushed__array_matches_expected(
    void)
{
    const uint8_t byte = 0x05;

    nexus_bitstream_init(&_this.stream, _this.data, sizeof(_this.data) * 8, 0);
    nexus_bitstream_push_uint8(&_this.stream, byte, 3);

    TEST_ASSERT_EQUAL_UINT(nexus_bitstream_length_in_bits(&_this.stream), 3);
    TEST_ASSERT_EQUAL_UINT(_this.data[0], byte << 5);
}

void test_nexus_bitstream_push_uint8__multiple_steps__array_matches_expected(
    void)
{
    nexus_bitstream_init(
        &_this.stream, _this.data, sizeof(EXAMPLE_BYTES) * 8, 0);

    for (uint8_t i = 0; i < sizeof(EXAMPLE_LENGTHS); ++i)
    {
        nexus_bitstream_push_uint8(
            &_this.stream, EXAMPLE_INPUTS[i], EXAMPLE_LENGTHS[i]);
    }

    for (uint8_t i = 0; i < sizeof(EXAMPLE_BYTES); ++i)
    {
        TEST_ASSERT_EQUAL_UINT(_this.data[i], EXAMPLE_BYTES[i]);
    }
}

void test_nexus_bitstream_pull_uint8__trivial_bits_pulled__results_matches_expected(
    void)
{
    uint8_t byte = 0x85;

    nexus_bitstream_init(&_this.stream, &byte, sizeof(byte) * 8, 8);

    TEST_ASSERT_EQUAL_UINT(nexus_bitstream_pull_uint8(&_this.stream, 3), 0x04);
}

void test_nexus_bitstream_pull_uint8__input_provided__pulled_matches_input(void)
{
    memcpy(_this.data, EXAMPLE_BYTES, sizeof(EXAMPLE_BYTES));

    nexus_bitstream_init(&_this.stream,
                         _this.data,
                         sizeof(EXAMPLE_BYTES) * 8,
                         sizeof(EXAMPLE_BYTES) * 8);

    for (uint8_t i = 0; i < sizeof(EXAMPLE_LENGTHS); ++i)
    {
        const uint8_t pulled =
            nexus_bitstream_pull_uint8(&_this.stream, EXAMPLE_LENGTHS[i]);

        TEST_ASSERT_EQUAL_UINT(pulled, EXAMPLE_INPUTS[i]);
    }
}

void test_nexus_bitstream_pull_uint16_be__input_provided__pulled_matches_input(
    void)
{
    uint8_t bytes[] = {0x5a, 0x81, 0xed};
    const uint8_t lengths[] = {12, 4, 8};
    const uint16_t results[] = {0x05a8, 0x0001, 0x00ed};

    nexus_bitstream_init(
        &_this.stream, bytes, sizeof(bytes) * 8, sizeof(bytes) * 8);

    for (uint8_t i = 0; i < sizeof(lengths); ++i)
    {
        const uint16_t pulled =
            nexus_bitstream_pull_uint16_be(&_this.stream, lengths[i]);

        TEST_ASSERT_EQUAL_UINT(pulled, results[i]);
    }
}

void test_nexus_check_compute__fixed_inputs__outputs_are_expected(void)
{
    const struct nx_check_key input_keys[] = {
        {{0x00,
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
          0x00}},
        {{0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1}},
        {{0x00,
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
          0x00}},
        {{0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1,
          0xd1}},
        {{0x12,
          0x34,
          0x56,
          0x78,
          0x9a,
          0xbc,
          0xde,
          0xfe,
          0x01,
          0x23,
          0x45,
          0x67,
          0x89,
          0xdd,
          0xcc,
          0xfe}},
    };
    const void* input_data_pointers[] = {
        "", "", "qwerty", "qwerty", "qwerty",
    };
    const uint16_t input_data_sizes[] = {
        0, 0, 6, 6, 6,
    };
    const uint64_t expected_values[] = {
        0x1e924b9d737700d7,
        0xb9cbdc781f16d561,
        0x5ac1de9410957ea6,
        0xcf6606899425c75c,
        0x5b124e614c6b3e3f,
    };

    for (uint8_t i = 0; i < sizeof(input_keys) / sizeof(input_keys[0]); ++i)
    {
        const struct nexus_check_value value = nexus_check_compute(
            &input_keys[i], input_data_pointers[i], input_data_sizes[i]);

        TEST_ASSERT_EQUAL_UINT(*(uint64_t*) value.bytes, expected_values[i]);
    }
}

void test_nexus_check_compute_pseudorandom_bytes__fixed_inputs__outputs_are_expected(
    void)
{
    struct test_scenario
    {
        const void* input;
        uint16_t input_size;
        const void* expected;
        uint16_t expected_size;
    };

    const struct test_scenario scenarios[] = {
        // expected values taken from Python implementation
        {"\x70", 1, "\x24\x54", 2},
        {"\x60", 1, "\x05\x09", 2},
        {"", 0, "\x8d\xc5", 2},
        {"\x8a\x91\xab\xff", 4, "\xdf\x0a", 2},
        {"\x70", 1, "\x24\x54\x7f\xec\x23\xcf\x0d\xa8", 8},
        {"\xa9\x90\x41", 3, "\x5f\xe2\x44", 3},
        {"\x06\xfa", 2, "\x00\xb9", 2}};

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        const struct test_scenario scenario = scenarios[i];
        uint8_t output[scenario.expected_size];

        nexus_check_compute_pseudorandom_bytes(
            &NEXUS_INTEGRITY_CHECK_FIXED_00_KEY,
            scenario.input,
            scenario.input_size,
            output,
            scenario.expected_size);

        TEST_ASSERT_EQUAL_HEX8_ARRAY(
            &output, scenario.expected, scenario.expected_size);
    }
}

void test_nexus_endian_htobe16__fixed_inputs__outputs_are_expected(void)
{
    struct test_scenario
    {
        const uint16_t input;
    };

    const struct test_scenario scenarios[] = {{0}, {24}, {65534}};

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        // not a great test because we're stuck with our host byte order
        const struct test_scenario scenario = scenarios[i];
        const uint16_t output = nexus_endian_htobe16(scenario.input);

        TEST_ASSERT_EQUAL_UINT(output, htons(scenario.input));
    }
}

void test_nexus_endian_be16toh__fixed_inputs__outputs_are_expected(void)
{
    struct test_scenario
    {
        const uint16_t input;
    };

    const struct test_scenario scenarios[] = {{0}, {24}, {65534}};

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        // not a great test because we're stuck with our host byte order
        const struct test_scenario scenario = scenarios[i];
        const uint16_t output = nexus_endian_be16toh(scenario.input);

        TEST_ASSERT_EQUAL_UINT(output, ntohs(scenario.input));
    }
}

void test_nexus_digits_init__various_lengths__data_as_expected(void)
{

    struct test_scenario
    {
        const char* input_chars;
        const uint8_t length;
    };
    const struct test_scenario scenarios[] = {
        {"123456789", 9}, {"!", 1}, {"02838844499922", 14}};

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        struct nexus_digits digits;
        nexus_digits_init(
            &digits, scenarios[i].input_chars, scenarios[i].length);
        TEST_ASSERT_EQUAL_UINT(scenarios[i].length, digits.length);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(
            scenarios[i].input_chars, digits.chars, digits.length);
    }
}

void test_nexus_digits_pull_uint32__single_digit_pulled__result_ok(void)
{
    struct nexus_digits digits = {
        "02838844499922", // chars
        14, // length
        0, // position
    };

    const uint32_t result = nexus_digits_pull_uint32(&digits, 1);
    TEST_ASSERT_EQUAL_UINT(0, result);
    TEST_ASSERT_EQUAL_HEX8_ARRAY("2838844499922",
                                 digits.chars + digits.position,
                                 digits.length - digits.position);
}

void test_nexus_digits_pull_uint32__six_digits_pulled__result_ok(void)
{
    struct nexus_digits digits = {
        "02838844499922", // chars
        14, // length
        0, // position
    };

    const uint32_t result = nexus_digits_pull_uint32(&digits, 6);
    TEST_ASSERT_EQUAL_UINT(28388, result);
    TEST_ASSERT_EQUAL_HEX8_ARRAY("44499922",
                                 digits.chars + digits.position,
                                 digits.length - digits.position);
}

void test_nexus_digits_try_pull_uint32__no_underrun__returns_same_as_pull_uint32(
    void)
{
    struct nexus_digits digits = {
        "02838844499922", // chars
        14, // length
        0, // position
    };

    bool underrun = false;

    const uint32_t result = nexus_digits_try_pull_uint32(&digits, 6, &underrun);
    TEST_ASSERT_EQUAL_UINT(28388, result);
    TEST_ASSERT_FALSE(underrun);
    TEST_ASSERT_EQUAL_HEX8_ARRAY("44499922",
                                 digits.chars + digits.position,
                                 digits.length - digits.position);
}

void test_nexus_digits_try_pull_uint32__underrun_already_set__returns_sentinel(
    void)
{
    struct nexus_digits digits = {
        "02838844499922", // chars
        14, // length
        0, // position
    };

    TEST_ASSERT_EQUAL_HEX8_ARRAY("02838844499922",
                                 digits.chars + digits.position,
                                 digits.length - digits.position);

    bool underrun = true;

    const uint32_t result = nexus_digits_try_pull_uint32(&digits, 6, &underrun);
    TEST_ASSERT_EQUAL_UINT(UINT32_MAX, result);
    TEST_ASSERT_TRUE(underrun);

    // digits unmodified
    TEST_ASSERT_EQUAL_HEX8_ARRAY("02838844499922",
                                 digits.chars + digits.position,
                                 digits.length - digits.position);
}
void test_nexus_digits_try_pull_uint32__too_few_remaining_digits__sets_underrun_returns_sentinel(
    void)
{
    struct nexus_digits digits = {
        "02838844499922", // chars
        14, // length
        9, // position
    };

    bool underrun = false;

    TEST_ASSERT_EQUAL_HEX8_ARRAY("99922",
                                 digits.chars + digits.position,
                                 digits.length - digits.position);

    const uint32_t result = nexus_digits_try_pull_uint32(&digits, 6, &underrun);
    TEST_ASSERT_EQUAL_UINT(UINT32_MAX, result); // failure sentinel
    TEST_ASSERT_TRUE(underrun);

    // test digits is unmodified
    TEST_ASSERT_EQUAL_HEX8_ARRAY("99922",
                                 digits.chars + digits.position,
                                 digits.length - digits.position);
}

void test_nexus_bitset_init__various_bytes__result_matches(void)
{
    struct test_scenario
    {
        uint8_t input_bytes[10];
        uint8_t bytes_count;
    };
    struct test_scenario scenarios[] = {{{0, 255, 10, 20, 30, 40, 50, 100}, 8},
                                        {{255}, 1}};

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        struct nexus_bitset bitset;
        nexus_bitset_init(
            &bitset, scenarios[i].input_bytes, scenarios[i].bytes_count);
        TEST_ASSERT_EQUAL_UINT(scenarios[i].bytes_count, bitset.bytes_count);
        TEST_ASSERT_EQUAL_UINT_ARRAY(
            scenarios[i].input_bytes, bitset.bytes, bitset.bytes_count);
    }
}

void test_nexus_bitset_add_bitset__check_bitset_after__contains_expected_result(
    void)
{
    struct test_scenario
    {
        uint8_t bytes_before[3];
        uint16_t add_element;
        uint8_t bytes_after[3];
    };
    struct test_scenario scenarios[] = {{{0, 0, 0}, 0, {1, 0, 0}},
                                        {{0, 0, 0}, 7, {128, 0, 0}},
                                        {{0, 0, 0}, 23, {0, 0, 128}},
                                        {{127, 127, 127}, 23, {127, 127, 255}},
                                        {{255, 127, 127}, 7, {255, 127, 127}}};

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        struct nexus_bitset bitset;
        nexus_bitset_init(&bitset, scenarios[i].bytes_before, 3);
        nexus_bitset_add(&bitset, scenarios[i].add_element);

        TEST_ASSERT_EQUAL_HEX8_ARRAY(scenarios[i].bytes_after, bitset.bytes, 3);
    }
}

void test_nexus_bitset_remove_bitset__check_bitset_after__contains_expected_result(
    void)
{
    struct test_scenario
    {
        uint8_t bytes_before[3];
        uint16_t remove_element;
        uint8_t bytes_after[3];
    };
    struct test_scenario scenarios[] = {{{0, 0, 0}, 0, {0, 0, 0}},
                                        {{128, 0, 0}, 7, {0, 0, 0}},
                                        {{0, 0, 128}, 23, {0, 0, 0}},
                                        {{127, 127, 127}, 23, {127, 127, 127}},
                                        {{127, 127, 127}, 3, {119, 127, 127}},
                                        {{255, 127, 127}, 7, {127, 127, 127}}};

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        struct nexus_bitset bitset;
        nexus_bitset_init(&bitset, scenarios[i].bytes_before, 3);
        nexus_bitset_remove(&bitset, scenarios[i].remove_element);

        TEST_ASSERT_EQUAL_HEX8_ARRAY(scenarios[i].bytes_after, bitset.bytes, 3);
    }
}

void test_nexus_bitset_contains_bitset__fixed_sets__contains_expected_result(
    void)
{
    struct test_scenario
    {
        uint8_t bytes_before[3];
        uint16_t contained_element;
        uint16_t absent_element;
    };
    struct test_scenario scenarios[] = {{{128, 0, 0}, 7, 0},
                                        {{0, 0, 128}, 23, 22},
                                        {{127, 127, 127}, 6, 7},
                                        {{127, 127, 127}, 14, 15},
                                        {{127, 127, 127}, 22, 23}};

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        struct nexus_bitset bitset;
        nexus_bitset_init(&bitset, scenarios[i].bytes_before, 3);
        TEST_ASSERT_TRUE(
            nexus_bitset_contains(&bitset, scenarios[i].contained_element));
        TEST_ASSERT_FALSE(
            nexus_bitset_contains(&bitset, scenarios[i].absent_element));
    }
}
