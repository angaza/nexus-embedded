#include "src/nexus_core_internal.h"
#include "src/nexus_keycode_core.h"
#include "src/nexus_keycode_mas.h"
#include "src/nexus_keycode_pro.h"
#include "src/nexus_nv.h"
#include "src/nexus_util.h"
#include "unity.h"
#include "utils/crc_ccitt.h"
#include "utils/siphash_24.h"

// Other support libraries
#include <mock_nexus_channel_core.h>
#include <mock_nxp_core.h>
#include <mock_nxp_keycode.h>
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

/********************************************************
 * PRIVATE FUNCTIONS
 *******************************************************/
// convenience functions to fill a frame

// Setup (called before any 'test_*' function is called, automatically)
void setUp(void)
{
    nxp_core_nv_read_IgnoreAndReturn(true);
    nxp_core_nv_write_IgnoreAndReturn(true);

    // use full arbitrarily
    const struct nexus_keycode_handling_config full_config = {
        nexus_keycode_pro_full_parse_and_apply,
        nexus_keycode_pro_full_init,
        NEXUS_KEYCODE_PROTOCOL_NO_STOP_LENGTH,
        '*',
        '#',
        "0123456789"};

    _nexus_keycode_core_internal_init(&full_config);
}

// Teardown (called after any 'test_*' function is called, automatically)
void tearDown(void)
{
}

void test_mask_below_message_id__id_0__no_mask_changes(void)
{
    nexus_keycode_pro_mask_below_message_id(0);

    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_current_pd_index(), 23);

    for (uint8_t i = 0; i < 63; i++)
    {
        TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_full_message_id_flag(i),
                               0);
    }
}

void test_mask_below_message_id__id_below_current_window__no_mask_changes(void)
{
    nexus_keycode_pro_set_full_message_id_flag(301);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_current_pd_index(), 301);

    // below the window
    nexus_keycode_pro_mask_below_message_id(200);

    for (uint32_t i = 301 - NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_BEFORE_PD;
         i < 301 + NEXUS_KEYCODE_PRO_RECEIVE_WINDOW_AFTER_PD;
         i++)
    {
        if (i != 301)
        {
            TEST_ASSERT_EQUAL_UINT(
                nexus_keycode_pro_get_full_message_id_flag(i), 0);
        }
    }
}

void test_mask_idx_from_message_id__full_message_id_above_pd__returns_correct_mask(
    void)
{
    nexus_keycode_pro_set_full_message_id_flag(301);
    TEST_ASSERT_EQUAL_UINT(nexus_keycode_pro_get_current_pd_index(), 301);

    uint8_t mask_id_index;

    // below the window
    bool result =
        nexus_keycode_pro_mask_idx_from_message_id(302, &mask_id_index);

    TEST_ASSERT_TRUE(result);
    // '1' above the PD/window center of 23, so 24.
    TEST_ASSERT_EQUAL_UINT(mask_id_index, 24);
}
