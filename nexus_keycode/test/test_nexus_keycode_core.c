#include "include/common/crc_ccitt.h"
#include "include/common/siphash_24.h"
#include "src/nexus_keycode_core.h"
#include "src/nexus_keycode_mas.h"
#include "src/nexus_keycode_pro.h"
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

/********************************************************
 * PRIVATE FUNCTIONS
 *******************************************************/

// Setup (called before any 'test_*' function is called, automatically)
void setUp(void)
{
    port_nv_read_IgnoreAndReturn(true);
    port_nv_write_IgnoreAndReturn(true);
    port_request_processing_Expect();
    nx_keycode_init();
    TEST_ASSERT_FALSE(nexus_keycode_core_init_completed());
    nx_keycode_process(0);
    TEST_ASSERT_TRUE(nexus_keycode_core_init_completed());
}

// Teardown (called after any 'test_*' function is called, automatically)
void tearDown(void)
{
}

void test_keycode_core_uptime__uptime_error_on_invalid_value__ok(void)
{
    TEST_ASSERT_EQUAL(0, nexus_keycode_core_uptime());
    TEST_ASSERT_EQUAL(0, nexus_keycode_core_uptime());
    nx_keycode_process(40);
    TEST_ASSERT_EQUAL(40, nexus_keycode_core_uptime());
    // 10 is in the past compared to 40
    nx_keycode_process(10);
    TEST_ASSERT_EQUAL(40, nexus_keycode_core_uptime());
}

void test_keycode_core_uptime__uptime_increments_to_max_values__ok(void)
{
    // Count up to 130+ years in seconds without rollover
    for (uint32_t i = 0; i < UINT32_MAX; i += UINT32_MAX / 3)
    {
        nx_keycode_process(i);
        TEST_ASSERT_EQUAL(i, nexus_keycode_core_uptime());
    }
}
