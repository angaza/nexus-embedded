#include "include/nx_channel.h"
#include "include/nxp_core.h"

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
#include "utils/oc_list.h"
#include "utils/oc_uuid.h"

#include "src/internal_channel_config.h"
#include "src/nexus_channel_core.h"
#include "src/nexus_channel_om.h"
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
#include "utils/crc_ccitt.h"
#include "utils/siphash_24.h"

#include "unity.h"

// Other support libraries
#include <mock_nexus_channel_payg_credit.h>
#include <mock_nxp_channel.h>
#include <mock_nxp_core.h>
#include <mock_nxp_keycode.h>
#include <mock_oc_clock.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// pull in source file from IoTivity without changing its name
// https://github.com/ThrowTheSwitch/Ceedling/issues/113
TEST_FILE("oc/api/oc_server_api.c")
TEST_FILE("oc/api/oc_client_api.c")
TEST_FILE("oc/deps/tinycbor/cborencoder.c")
TEST_FILE("oc/deps/tinycbor/cborparser.c")

/********************************************************
 * DEFINITIONS
 *******************************************************/
static oc_message_t* G_OC_MESSAGE = 0;
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
    G_OC_MESSAGE = oc_allocate_message();
}

// Teardown (called after any 'test_*' function is called, automatically)
void tearDown(void)
{
    oc_message_unref(G_OC_MESSAGE);
}

void test_nexus_oc_wrapper__oc_endpoint_to_nx_ipv6__various_scenarios__ok(void)
{
    struct test_scenario
    {
        const struct oc_endpoint_t input;
        const struct nx_ipv6_address expected;
    };

    const struct test_scenario scenarios[] = {
        {
            {0, // no 'next' endpoint
             0, // arbitrary device ID
             IPV6, // flag from oc_endpoint
             {0}, // uuid 'di' not used
             {
                 5683, // port
                 {0xFE, /// valid source address
                  0x80,
                  0,
                  0,
                  0,
                  0,
                  0,
                  0,
                  0x02,
                  0,
                  0x12,
                  0xFF,
                  0xFE,
                  0x34,
                  0x56,
                  0x78},
                 2, // scope = link-local
             },
             {0}, // `addr_local` unused
             0, // `interface_index` unused
             0, // `priority` unused
             OIC_VER_1_1_0},
            {
                {0xFE,
                 0x80,
                 0,
                 0,
                 0,
                 0,
                 0,
                 0,
                 0x02,
                 0,
                 0x12,
                 0xFF,
                 0xFE,
                 0x34,
                 0x56,
                 0x78},
                false, // t/f for global scope (link-local scope here)
            }, // nx_ipv6 address, global scope bool
        },
        {
            {0, // no 'next' endpoint
             0, // arbitrary device ID
             IPV6, // flag from oc_endpoint
             {0}, // uuid 'di' not used
             {
                 5683, // port
                 {0xAA,
                  0xBB,
                  0xFF,
                  0,
                  0,
                  0,
                  0,
                  0,
                  0,
                  0,
                  0x12,
                  0xEF,
                  0xDA,
                  0x34,
                  0x56,
                  0x78},
                 0, // scope = global
             },
             {0}, // `addr_local` unused
             0, // `interface_index` unused
             0, // `priority` unused
             OIC_VER_1_1_0},
            {
                {0xAA,
                 0xBB,
                 0xFF,
                 0,
                 0,
                 0,
                 0,
                 0,
                 0,
                 0,
                 0x12,
                 0xEF,
                 0xDA,
                 0x34,
                 0x56,
                 0x78},
                true, // t/f for global scope (link-local scope here)
            }, // nx_ipv6 address, global scope bool
        },
    };

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        const struct test_scenario scenario = scenarios[i];
        struct nx_ipv6_address output;
        nexus_oc_wrapper_oc_endpoint_to_nx_ipv6(&scenario.input, &output);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            scenario.expected.address, output.address, 16);
        TEST_ASSERT_EQUAL_UINT(scenario.expected.global_scope,
                               output.global_scope);
    }
}

void test_nexus_oc_wrapper__oc_endpoint_to_nx_id__various_scenarios__ok(void)
{
    struct test_scenario
    {
        const struct oc_endpoint_t input;
        const struct nx_id expected;
    };

    const struct test_scenario scenarios[] = {
        {
            {0, // no 'next' endpoint
             0, // arbitrary device ID
             IPV6, // flag from oc_endpoint
             {0}, // uuid 'di' not used
             {
                 5683, // port
                 {0xFE, /// valid source address
                  0x80,
                  0,
                  0,
                  0,
                  0,
                  0,
                  0,
                  0x02,
                  0,
                  0x12,
                  0xFF,
                  0xFE,
                  0x34,
                  0x56,
                  0x78},
                 2, // scope = link-local
             },
             {0}, // `addr_local` unused
             0, // `interface_index` unused
             0, // `priority` unused
             OIC_VER_1_1_0},
            {0x0000, 0x12345678},
        },
        {
            {0, // no 'next' endpoint
             0, // arbitrary device ID
             IPV6, // flag from oc_endpoint
             {0}, // uuid 'di' not used
             {
                 5683, // port
                 {0xAA,
                  0xBB,
                  0xFF,
                  0,
                  0,
                  0,
                  0,
                  0,
                  0x02,
                  0,
                  0x12,
                  0xFF,
                  0xFE,
                  0x34,
                  0x56,
                  0x78},
                 0, // scope = global (does not impact nx_id)
             },
             {0}, // `addr_local` unused
             0, // `interface_index` unused
             0, // `priority` unused
             OIC_VER_1_1_0},
            {0x0000, 0x12345678},
        },
    };

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        const struct test_scenario scenario = scenarios[i];
        struct nx_id output;
        bool success =
            nexus_oc_wrapper_oc_endpoint_to_nx_id(&scenario.input, &output);
        TEST_ASSERT_EQUAL_UINT(scenario.expected.authority_id,
                               output.authority_id);
        TEST_ASSERT_EQUAL_UINT(scenario.expected.device_id, output.device_id);
    }
}

void test_nexus_oc_wrapper__nx_channel_network_receive__invalid_messages__rejected(
    void)
{
    struct nx_id fake_id = {0, 12345678};
    struct nx_ipv6_address fake_origin_address;
    uint8_t dummy_data[10];
    memset(&dummy_data, 0xAB, sizeof(dummy_data));
    nx_core_nx_id_to_ipv6_address(&fake_id, &fake_origin_address);

    nx_channel_error result =
        nx_channel_network_receive(NULL, 0, &fake_origin_address);
    TEST_ASSERT_EQUAL_UINT(NX_CHANNEL_ERROR_UNSPECIFIED, result);
    result = nx_channel_network_receive(NULL, 1, &fake_origin_address);
    TEST_ASSERT_EQUAL_UINT(NX_CHANNEL_ERROR_UNSPECIFIED, result);
    result = nx_channel_network_receive(dummy_data, 0, &fake_origin_address);
    TEST_ASSERT_EQUAL_UINT(NX_CHANNEL_ERROR_UNSPECIFIED, result);
}

void test_nexus_oc_wrapper__nx_channel_network_receive__valid_message__no_error(
    void)
{
    struct nx_id fake_id = {0, 12345678};
    struct nx_ipv6_address fake_origin_address;
    uint8_t dummy_data[10];
    memset(&dummy_data, 0xAB, sizeof(dummy_data));
    nx_core_nx_id_to_ipv6_address(&fake_id, &fake_origin_address);

    nxp_core_request_processing_Expect(); // due to a valid message being
    // received
    nx_channel_error result =
        nx_channel_network_receive(dummy_data, 10, &fake_origin_address);
    TEST_ASSERT_EQUAL_UINT(NX_CHANNEL_ERROR_NONE, result);
}

void test_nexus_oc_wrapper__oc_send_buffer__expected_calls_to_nxp_channel_network_send(
    void)
{
    // we assert that this flag is set
    G_OC_MESSAGE->endpoint.flags = IPV6 | MULTICAST;
    struct nx_id fake_id = {0, 12345678};
    struct nx_ipv6_address expected_source_address;
    struct nx_ipv6_address expected_dest_address;
    nx_core_nx_id_to_ipv6_address(&fake_id, &expected_source_address);
    nexus_oc_wrapper_oc_endpoint_to_nx_ipv6(&G_OC_MESSAGE->endpoint,
                                            &expected_dest_address);

    nxp_channel_get_nexus_id_ExpectAndReturn(fake_id);

    nxp_channel_network_send_ExpectAndReturn(
        G_OC_MESSAGE->data,
        G_OC_MESSAGE->length,
        &expected_source_address,
        &expected_dest_address,
        true, // we set the endpoint flags to "MULTICAST"
        NX_CHANNEL_ERROR_NONE);

    oc_send_buffer(G_OC_MESSAGE);
}

void test_nexus_oc_wrapper__oc_send_discovery_request__identical_to_send_buffer(
    void)
{
    // we assert that this flag is set
    G_OC_MESSAGE->endpoint.flags = IPV6;
    struct nx_id fake_id = {0, 12345678};
    struct nx_ipv6_address expected_source_address;
    struct nx_ipv6_address expected_dest_address;
    nx_core_nx_id_to_ipv6_address(&fake_id, &expected_source_address);
    nexus_oc_wrapper_oc_endpoint_to_nx_ipv6(&G_OC_MESSAGE->endpoint,
                                            &expected_dest_address);

    nxp_channel_get_nexus_id_ExpectAndReturn(fake_id);

    nxp_channel_network_send_ExpectAndReturn(G_OC_MESSAGE->data,
                                             G_OC_MESSAGE->length,
                                             &expected_source_address,
                                             &expected_dest_address,
                                             false, // we didn't
                                             NX_CHANNEL_ERROR_NONE);

    oc_send_discovery_request(G_OC_MESSAGE);
}
