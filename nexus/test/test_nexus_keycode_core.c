#include "src/nexus_core_internal.h"
#include "src/nexus_keycode_core.h"
#include "src/nexus_keycode_mas.h"
#include "src/nexus_keycode_pro.h"
#include "src/nexus_keycode_util.h"
#include "src/nexus_nv.h"
#include "unity.h"
#include "utils/crc_ccitt.h"
#include "utils/siphash_24.h"

// Other support libraries
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

// Setup (called before any 'test_*' function is called, automatically)
void setUp(void)
{
    nxp_core_nv_read_IgnoreAndReturn(true);
    nxp_core_nv_write_IgnoreAndReturn(true);
}

// Teardown (called after any 'test_*' function is called, automatically)
void tearDown(void)
{
}

void test_keycode_core_init__completes_after_init__ok(void)
{
    TEST_ASSERT_FALSE(nexus_keycode_core_init_completed());
    nexus_keycode_core_init();
    TEST_ASSERT_TRUE(nexus_keycode_core_init_completed());
}