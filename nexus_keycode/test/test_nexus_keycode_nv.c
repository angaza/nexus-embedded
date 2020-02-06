#include "include/common/crc_ccitt.h"
#include "include/common/siphash_24.h"
#include "include/nx_keycode.h"
#include "src/nexus_keycode_util.h"
#include "src/nexus_nv.h"
#include "unity.h"

// Other support libraries
#include <mock_nexus_keycode_port.h>
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
// valid block 0 read
uint8_t block_0_valid[NX_NV_BLOCK_0_LENGTH] = {
    0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x89, 0x29};

// block 0 but block ID is incorrect
uint8_t block_0_bl_id_invalid[NX_NV_BLOCK_0_LENGTH] = {
    0x00, 0x01, 0x06, 0x00, 0x00, 0x00, 0x89, 0x29};

uint8_t block_0_crc_invalid[NX_NV_BLOCK_0_LENGTH] = {
    0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x90, 0x29};
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

void test_keycode_nv__block_meta_structs__ok(void)
{
    TEST_ASSERT_EQUAL(0, NX_NV_BLOCK_KEYCODE_MAS.block_id);
    TEST_ASSERT_EQUAL(NX_NV_BLOCK_0_LENGTH, NX_NV_BLOCK_KEYCODE_MAS.length);
    TEST_ASSERT_EQUAL(1, NX_NV_BLOCK_KEYCODE_PRO.block_id);
    TEST_ASSERT_EQUAL(NX_NV_BLOCK_1_LENGTH, NX_NV_BLOCK_KEYCODE_PRO.length);
}

void test_keycode_nv__validate_block__ok(void)
{
    TEST_ASSERT_TRUE(nx_nv_block_valid(NX_NV_BLOCK_KEYCODE_MAS, block_0_valid));
}

void test_keycode_nv__validate_block__block_id_mismatch_fail(void)
{
    TEST_ASSERT_FALSE(
        nx_nv_block_valid(NX_NV_BLOCK_KEYCODE_MAS, block_0_bl_id_invalid));
}

void test_keycode_nv__validate_block__block_crc_mismatch_fail(void)
{
    TEST_ASSERT_FALSE(
        nx_nv_block_valid(NX_NV_BLOCK_KEYCODE_MAS, block_0_crc_invalid));
}

void test_keycode_nv__read_block__valid_block_ok(void)
{
    uint8_t inner_data[NX_NV_BLOCK_0_LENGTH -
                       NEXUS_NV_BLOCK_WRAPPER_SIZE_BYTES] = {0};

    port_nv_read_ExpectAndReturn(NX_NV_BLOCK_KEYCODE_MAS, inner_data, true);
    port_nv_read_ReturnArrayThruPtr_read_buffer(block_0_valid,
                                                sizeof(block_0_valid));

    // `nexus_nv_read` only writes 'inner data'
    TEST_ASSERT_TRUE(nexus_nv_read(NX_NV_BLOCK_KEYCODE_MAS, inner_data));

    // Ensure *inner_data* copied matches (
    // `nexus_nv_read` must not copy the ID and CRC)
    for (uint8_t i = 0; i < sizeof(inner_data); i++)
    {
        // offset by 2 to skip the block ID section in block_0_valid
        TEST_ASSERT_EQUAL(block_0_valid[i + 2], inner_data[i]);
    }
}

void test_keycode_nv__read_block__block_invalid_fails(void)
{
    uint8_t data[NX_NV_BLOCK_0_LENGTH] = {0};

    port_nv_read_ExpectAndReturn(NX_NV_BLOCK_KEYCODE_MAS, data, true);
    port_nv_read_ReturnArrayThruPtr_read_buffer(block_0_crc_invalid,
                                                sizeof(block_0_crc_invalid));

    TEST_ASSERT_FALSE(nexus_nv_read(NX_NV_BLOCK_KEYCODE_MAS, data));

    // no data copied
    for (uint8_t i = 0; i < sizeof(data); i++)
    {
        TEST_ASSERT_EQUAL(data[i], 0x00);
    }
}

void test_keycode_nv__write_block__update_old_valid_block_ok(void)
{
    uint8_t block_0_old_valid[NX_NV_BLOCK_0_LENGTH] = {
        0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x55, 0xB2};

    port_nv_read_ExpectAnyArgsAndReturn(true);
    port_nv_read_ReturnArrayThruPtr_read_buffer(block_0_old_valid,
                                                sizeof(block_0_old_valid));

    port_nv_write_ExpectAndReturn(NX_NV_BLOCK_KEYCODE_MAS, block_0_valid, true);

    TEST_ASSERT_TRUE(nexus_nv_update(NX_NV_BLOCK_KEYCODE_MAS, block_0_valid));
}

void test_keycode_nv__write_block__old_block_identical_no_write(void)
{
    port_nv_read_ExpectAnyArgsAndReturn(true);
    port_nv_read_ReturnArrayThruPtr_read_buffer(block_0_valid,
                                                sizeof(block_0_valid));

    uint8_t block_0_inner_valid[NX_NV_BLOCK_0_LENGTH -
                                NEXUS_NV_BLOCK_WRAPPER_SIZE_BYTES] = {0};
    memcpy(block_0_inner_valid,
           block_0_valid + NEXUS_NV_BLOCK_ID_WIDTH,
           NX_NV_BLOCK_KEYCODE_MAS.length - NEXUS_NV_BLOCK_WRAPPER_SIZE_BYTES);

    TEST_ASSERT_TRUE(
        nexus_nv_update(NX_NV_BLOCK_KEYCODE_MAS, block_0_inner_valid));
}
