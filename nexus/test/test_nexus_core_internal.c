#include "include/nx_channel.h"
#include "messaging/coap/coap.h"
#include "messaging/coap/engine.h"
#include "messaging/coap/transactions.h"
#include "oc/api/oc_main.h"
#include "oc/include/oc_api.h"
#include "oc/include/oc_buffer.h"
#include "oc/include/oc_core_res.h"
#include "oc/include/oc_endpoint.h"
#include "oc/include/oc_helpers.h"
#include "oc/include/oc_network_events.h"
#include "oc/include/oc_rep.h"
#include "oc/include/oc_ri.h"
#include "oc/port/oc_connectivity.h"
#include "oc/util/oc_etimer.h"
#include "oc/util/oc_memb.h"
#include "oc/util/oc_mmem.h"
#include "oc/util/oc_process.h"
#include "oc/util/oc_timer.h"

#include "src/internal_channel_config.h"
#include "src/nexus_channel_core.h"
#include "src/nexus_channel_om.h"
#include "src/nexus_channel_payg_credit.h"
#include "src/nexus_channel_res_link_hs.h"
#include "src/nexus_channel_res_lm.h"
#include "src/nexus_channel_sm.h"
#include "src/nexus_core_internal.h"
#include "src/nexus_keycode_core.h"
#include "src/nexus_keycode_mas.h"
#include "src/nexus_keycode_pro.h"
#include "src/nexus_nv.h"
#include "src/nexus_oc_wrapper.h"
#include "src/nexus_security.h"
#include "src/nexus_util.h"
#include "unity.h"
#include "utils/crc_ccitt.h"
#include "utils/oc_list.h"
#include "utils/oc_uuid.h"
#include "utils/siphash_24.h"

// Other support libraries
#include <mock_nxp_channel.h>
#include <mock_nxp_core.h>
#include <mock_nxp_keycode.h>
#include <mock_oc_clock.h>
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
// pull in source file from IoTivity without changing its name
// https://github.com/ThrowTheSwitch/Ceedling/issues/113
TEST_FILE("oc/api/oc_server_api.c")
TEST_FILE("oc/api/oc_client_api.c")
TEST_FILE("oc/deps/tinycbor/cborencoder.c")
TEST_FILE("oc/deps/tinycbor/cborparser.c")

// Setup (called before any 'test_*' function is called, automatically)
void setUp(void)
{

    // Diagnostic only for quick functional check of logging.
    PRINT("Print output - Nexus Core Internal Setup\n");
    OC_DBG("OC_DEBUG");
    OC_WRN("OC_WRN");
    OC_ERR("OC_ERR");

    nxp_core_nv_read_IgnoreAndReturn(true);
    nxp_core_nv_write_IgnoreAndReturn(true);
    nxp_core_random_init_Ignore();
    nxp_core_random_value_IgnoreAndReturn(123456);
    nxp_core_request_processing_Expect();
    oc_clock_init_Ignore();
    nx_core_init();
    TEST_ASSERT_FALSE(nexus_core_init_completed());
    nx_core_process(0);
    TEST_ASSERT_TRUE(nexus_core_init_completed());
}

// Teardown (called after any 'test_*' function is called, automatically)
void tearDown(void)
{
    nx_core_shutdown();
}

void test_keycode_core_uptime__uptime_error_on_invalid_value__ok(void)
{
    TEST_ASSERT_EQUAL(0, nexus_core_uptime());
    TEST_ASSERT_EQUAL(0, nexus_core_uptime());
    nx_core_process(40);
    TEST_ASSERT_EQUAL(40, nexus_core_uptime());
    // 10 is in the past compared to 40
    nx_core_process(10);
    TEST_ASSERT_EQUAL(40, nexus_core_uptime());
}

void test_keycode_core_uptime__uptime_increments_to_max_values__ok(void)
{
    // Count up to 130+ years in seconds without rollover
    for (uint32_t i = 0; i < UINT32_MAX; i += UINT32_MAX / 3)
    {
        nx_core_process(i);
        TEST_ASSERT_EQUAL(i, nexus_core_uptime());
    }
}
