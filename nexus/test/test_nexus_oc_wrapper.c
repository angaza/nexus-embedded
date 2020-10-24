#include "include/nx_channel.h"
#include "include/nxp_common.h"

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
#include "src/nexus_common_internal.h"
#include "src/nexus_keycode_core.h"
#include "src/nexus_keycode_mas.h"
#include "src/nexus_keycode_pro.h"
#include "src/nexus_nv.h"
#include "src/nexus_oc_wrapper.h"
#include "src/nexus_security.h"
#include "src/nexus_util.h"
#include "utils/crc_ccitt.h"
#include "utils/siphash_24.h"

#include "test/test_platform_app.h"

#include "unity.h"

// Other support libraries
#include <mock_nexus_channel_res_payg_credit.h>
#include <mock_nxp_channel.h>
#include <mock_nxp_common.h>
#include <mock_nxp_keycode.h>
#include <mock_test_platform_app.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma clang diagnostic ignored "-Wshorten-64-to-32"

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
static const oc_interface_mask_t if_mask_arr[] = {OC_IF_BASELINE, OC_IF_RW};

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
    nexus_channel_core_shutdown();
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
             {{0}}, // uuid 'di' not used
             {{
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
             }},
             {{0}}, // `addr_local` unused
             0, // `interface_index` unused
             0, // `priority` unused
             OIC_VER_1_1_0},
            // on a LE system, this Nexus ID is stored in memory as
            // 0x000078563412
            {0x0000, 0x12345678},
        },
        {
            {0, // no 'next' endpoint
             0, // arbitrary device ID
             IPV6, // flag from oc_endpoint
             {{0}}, // uuid 'di' not used
             {{
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
             }},
             {{0}}, // `addr_local` unused
             0, // `interface_index` unused
             0, // `priority` unused
             OIC_VER_1_1_0},
            // on a LE system, this Nexus ID is stored in memory as
            // 0x000078563412
            {0x0000, 0x12345678},
        },
    };

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        const struct test_scenario scenario = scenarios[i];
        struct nx_id output;
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
    uint8_t dummy_data[200];
    memset(&dummy_data, 0xAB, sizeof(dummy_data));

    nx_channel_error result = nx_channel_network_receive(NULL, 0, &fake_id);
    TEST_ASSERT_EQUAL_UINT(NX_CHANNEL_ERROR_UNSPECIFIED, result);
    result = nx_channel_network_receive(NULL, 1, &fake_id);
    TEST_ASSERT_EQUAL_UINT(NX_CHANNEL_ERROR_UNSPECIFIED, result);
    result = nx_channel_network_receive(dummy_data, 0, &fake_id);
    TEST_ASSERT_EQUAL_UINT(NX_CHANNEL_ERROR_UNSPECIFIED, result);
    result = nx_channel_network_receive(
        dummy_data,
        NEXUS_CHANNEL_APPLICATION_LAYER_MAX_MESSAGE_BYTES + 1,
        &fake_id);
    TEST_ASSERT_EQUAL_UINT(NX_CHANNEL_ERROR_MESSAGE_TOO_LARGE, result);
}

void test_nexus_oc_wrapper__nx_channel_network_receive__valid_message__no_error(
    void)
{
    // We may tangentially trigger events in security manager tests, ignore
    nxp_channel_notify_event_Ignore();
    nxp_common_nv_read_IgnoreAndReturn(true);
    nxp_common_nv_write_IgnoreAndReturn(true);
    nxp_channel_random_value_IgnoreAndReturn(123456);
    nexus_channel_core_init();

    struct nx_id fake_id = {0, 12345678};
    uint8_t dummy_data[10];
    memset(&dummy_data, 0xAB, sizeof(dummy_data));

    nxp_common_request_processing_Expect(); // due to a valid message being
    // received
    nx_channel_error result =
        nx_channel_network_receive(dummy_data, 10, &fake_id);
    TEST_ASSERT_EQUAL_UINT(NX_CHANNEL_ERROR_NONE, result);

    // process the message to unref the internally-made ref
    nexus_channel_core_process(1);
}

void test_nexus_oc_wrapper__oc_send_buffer__expected_calls_to_nxp_channel_network_send(
    void)
{
    // we assert that this flag is set
    G_OC_MESSAGE->endpoint.flags = IPV6 | MULTICAST;
    G_OC_MESSAGE->length = NEXUS_CHANNEL_APPLICATION_LAYER_MAX_MESSAGE_BYTES;
    struct nx_id fake_source_nx_id = {0, 12345678};
    nxp_channel_get_nexus_id_ExpectAndReturn(fake_source_nx_id);

    nxp_channel_network_send_ExpectAndReturn(
        G_OC_MESSAGE->data,
        (uint32_t) G_OC_MESSAGE->length,
        &fake_source_nx_id,
        &NEXUS_OC_WRAPPER_MULTICAST_NX_ID,
        true, // we set the endpoint flags to "MULTICAST" above
        NX_CHANNEL_ERROR_NONE);

    oc_send_buffer(G_OC_MESSAGE);
}

void test_nexus_oc_wrapper__oc_send_buffer__message_too_large__does_not_call_nxp_channel_network_send(
    void)
{
    // we assert that this flag is set
    G_OC_MESSAGE->endpoint.flags = IPV6;
    struct nx_id fake_source_nx_id = {0, 12345678};
    nxp_channel_get_nexus_id_ExpectAndReturn(fake_source_nx_id);

    G_OC_MESSAGE->length =
        NEXUS_CHANNEL_APPLICATION_LAYER_MAX_MESSAGE_BYTES + 1;
    // no `nxp_channel_network_send_ExpectAndReturn` indicates that the
    // too large message is dropped and not passed to the product link layer
    int result = oc_send_buffer(G_OC_MESSAGE);
    // nonzero return code
    TEST_ASSERT_EQUAL(1, result);
}

void test_nexus_oc_wrapper__oc_send_discovery_request__identical_to_send_buffer(
    void)
{
    // we assert that this flag is set
    G_OC_MESSAGE->endpoint.flags = IPV6;
    struct nx_id expected_source_nx_id = {0, 12345678};
    struct nx_id expected_dest_nx_id;
    nxp_channel_get_nexus_id_ExpectAndReturn(expected_source_nx_id);
    nexus_oc_wrapper_oc_endpoint_to_nx_id(&G_OC_MESSAGE->endpoint,
                                          &expected_dest_nx_id);

    nxp_channel_network_send_ExpectAndReturn(G_OC_MESSAGE->data,
                                             (uint32_t) G_OC_MESSAGE->length,
                                             &expected_source_nx_id,
                                             &expected_dest_nx_id,
                                             false, // we didn't set multicast
                                             NX_CHANNEL_ERROR_NONE);

    oc_send_discovery_request(G_OC_MESSAGE);
}

void test_nexus_oc_wrapper__repack_buffer_secured__repack_ok(void)
{
    const char* data = "hello world";
    uint8_t buf[50] = {0};
    memcpy(buf, data, strlen(data));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(buf, data, strlen(data));

    // populate COSE_MAC0 struct
    nexus_security_mode0_cose_mac0_t cose_mac0 = {0};
    cose_mac0.protected_header = OC_POST; // 2
    cose_mac0.kid = 0;
    cose_mac0.nonce = 5; // arbitrary
    cose_mac0.payload = buf;
    cose_mac0.payload_len = (uint8_t) strlen(data);
    struct nexus_check_value mac = {
        {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07}};
    cose_mac0.mac = &mac;

    nexus_oc_wrapper_repack_buffer_secured(buf, &cose_mac0);

    /* From cbor.me:
    BF                           # map(*)
        61                        # text(1)
            70                     # "p"
        41                        # bytes(1)
            02                     # "\x02"
        61                        # text(1)
            75                     # "u"
        BF                        # map(*)
            61                     # text(1)
                34                  # "4"
            00                     # unsigned(0)
            61                     # text(1)
                35                  # "5"
            05                     # unsigned(5)
            FF                     # primitive(*)
        61                        # text(1)
            64                     # "d"
        4B                        # bytes(11)
            68656C6C6F20776F726C64 # "hello world"
        61                        # text(1)
            6D                     # "m"
        48                        # bytes(8)
            0001020304050607       # "\x00\x01\x02\x03\x04\x05\x06\a"
        FF                        # primitive(*)


        ##### 9 unused bytes after the end of the data item:

        00 00 00 00 00 00 00 00 00
    */

    uint8_t expected_secured_buf[50] = {
        0xBF, 0x61, 0x70, 0x41, 0x02, 0x61, 0x75, 0xBF, 0x61, 0x34,
        0x00, 0x61, 0x35, 0x05, 0xFF, 0x61, 0x64, 0x4B, 0x68, 0x65,
        0x6C, 0x6C, 0x6F, 0x20, 0x77, 0x6F, 0x72, 0x6C, 0x64, 0x61,
        0x6D, 0x48, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(buf, expected_secured_buf, sizeof(buf));
}

void test_nexus_oc_wrapper_nx_id_to_oc_endpoint__various_scenarios__output_expected(
    void)
{
    struct test_scenario
    {
        const struct nx_id input;
        const struct oc_endpoint_t expected;
    };

    const struct test_scenario scenarios[] = {
        {
            // on a LE system, this Nexus ID is stored in memory as
            // 0x000078563412
            {0x0000, 0x12345678}, // authority ID, device ID
            {0, // no 'next' endpoint
             0, // arbitrary device ID
             IPV6, // flag from oc_endpoint
             {{0}}, // uuid 'di' not used
             {{
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
             }},
             {{0}}, // `addr_local` unused
             0, // `interface_index` unused
             0, // `priority` unused
             OIC_VER_1_1_0},
        },
        {
            // on a LE system, this Nexus ID is stored in memory as
            // 0x2010AB000000
            {0x1020, 0xAB}, // authority ID, device ID
            {0, // no 'next' endpoint
             0, // arbitrary device ID
             IPV6, // flag from oc_endpoint
             {{0}}, // uuid 'di' not used
             {{
                 5683, // port
                 {0xFE,
                  0x80,
                  0,
                  0,
                  0,
                  0,
                  0,
                  0,
                  0x12,
                  0x20,
                  0x00,
                  0xFF,
                  0xFE,
                  0,
                  0,
                  0xAB},
                 2, // local scope
             }},
             {{0}}, // `addr_local` unused
             0, // `interface_index` unused
             0, // `priority` unused
             OIC_VER_1_1_0},
        },
        {
            // on a LE system, this Nexus ID is stored in memory as
            // 0xACD22201FBFC
            {0xD2AC, 0xFCFB0122}, // authority ID, device ID
            {0, // no 'next' endpoint
             0, // arbitrary device ID
             IPV6, // flag from oc_endpoint
             {{0}}, // uuid 'di' not used
             {{
                 5683, // port
                 {0xFE,
                  0x80,
                  0,
                  0,
                  0,
                  0,
                  0,
                  0,
                  0xD0,
                  0xAC,
                  0xFC,
                  0xFF,
                  0xFE,
                  0xFB,
                  0x01,
                  0x22},
                 2, // local scope
             }},
             {{0}}, // `addr_local` unused
             0, // `interface_index` unused
             0, // `priority` unused
             OIC_VER_1_1_0},
        },
    };

    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        // not a great test because we're stuck with our host byte order
        const struct test_scenario scenario = scenarios[i];
        oc_endpoint_t output;
        nexus_oc_wrapper_nx_id_to_oc_endpoint(&scenario.input, &output);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            scenario.expected.addr.ipv6.address, output.addr.ipv6.address, 16);
        TEST_ASSERT_EQUAL_UINT(scenario.expected.addr.ipv6.scope,
                               output.addr.ipv6.scope);
    }
}

void test_nexus_oc_wrapper__nexus_channel_set_request_handler__unknown_method_fails(
    void)
{
    // We may tangentially trigger events in security manager tests, ignore
    nxp_channel_notify_event_Ignore();
    nxp_common_nv_read_IgnoreAndReturn(true);
    nxp_common_nv_write_IgnoreAndReturn(true);
    nxp_channel_random_value_IgnoreAndReturn(123456);

    nexus_channel_core_init();

    // register resource
    const struct nx_channel_resource_props pc_props = {
        .uri = "/nx/pc",
        .resource_type = "angaza.com.nexus.payg_credit",
        .rtr = 65000,
        .num_interfaces = 2,
        .if_masks = if_mask_arr,
        .get_handler = nexus_channel_res_payg_credit_get_handler,
        .get_secured = false,
        .post_handler = NULL,
        .post_secured = false};

    nx_channel_error reg_result = nx_channel_register_resource(&pc_props);

    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_NONE, reg_result);

    oc_resource_t* res = oc_ri_get_app_resource_by_uri(
        "/nx/pc", strlen("/nx/pc"), NEXUS_CHANNEL_NEXUS_DEVICE_ID);

    TEST_ASSERT_EQUAL(
        NX_CHANNEL_ERROR_METHOD_UNSUPPORTED,
        nexus_channel_set_request_handler(
            res, 5, nexus_channel_res_payg_credit_get_handler, false));
}

// CLIENT TESTS

void CALLBACK_test_nexus_oc_wrapper__nx_channel_do_get_post_request__callback_handler_check(
    nx_channel_client_response_t* response, int NumCalls)
{
    (void) NumCalls;

    struct nx_id expected_nx_id = {0xFFFF, 0x87654321};
    const char* context = "context";

    // only expect one element in the rep
    TEST_ASSERT_EQUAL_UINT(
        0, strncmp(oc_string(response->payload->name), "th", 2));
    TEST_ASSERT_EQUAL_UINT(20, response->payload->value.integer);
    TEST_ASSERT_EQUAL(NULL, response->payload->next);

    TEST_ASSERT_EQUAL_UINT(expected_nx_id.authority_id,
                           response->source->authority_id);
    TEST_ASSERT_EQUAL_UINT(expected_nx_id.device_id,
                           response->source->device_id);

    // 2.05 response code is converted to an oc_status_t
    TEST_ASSERT_EQUAL_UINT(OC_STATUS_OK, response->code);

    TEST_ASSERT_EQUAL_UINT(0, strncmp(response->request_context, context, 8));

    return;
}

void test_nexus_oc_wrapper__nx_channel_do_get_request__get_reply_success(void)
{
    // We may tangentially trigger events in security manager tests, ignore
    nxp_channel_notify_event_Ignore();
    nxp_common_nv_read_IgnoreAndReturn(true);
    nxp_common_nv_write_IgnoreAndReturn(true);
    nxp_channel_random_value_IgnoreAndReturn(123456);
    nexus_channel_core_init();

    struct nx_id src_nx_id = {0xFFFF, 0x12345678};
    struct nx_id dest_nx_id = {0xFFFF, 0x87654321};

    // register resource (server; replies)
    const struct nx_channel_resource_props pc_props = {
        .uri = "/test",
        .resource_type = "angaza.test",
        .rtr = 65000,
        .num_interfaces = 2,
        .if_masks = if_mask_arr,
        .get_handler = nexus_channel_res_payg_credit_get_handler,
        .get_secured = false,
        .post_handler = NULL,
        .post_secured = false};

    nx_channel_error reg_result = nx_channel_register_resource(&pc_props);

    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_NONE, reg_result);

    // arbitrary request context
    char* context = "context";

    // query string
    char* query = "th=15";

    // The request is (same token value due to random value mock above):
    // 51 01 E2 41 40 B4 74 65 73 74 45 74 68 3D 31 35
    // version 0x01, TKL 0x01, code 0x01, MID 0xE241, token 0x40, option 0xB4
    // (uri-path, length 4), option value 0x74657374 (ASCII "test")
    // option 45 (uri-query, length 5), option value 0x74683D3135 (ASCII
    // "th=15")
    uint8_t request_bytes[16] = {0x51,
                                 0x01,
                                 0xE2,
                                 0x41,
                                 0x40,
                                 0xB4,
                                 0x74,
                                 0x65,
                                 0x73,
                                 0x74,
                                 0x45,
                                 0x74,
                                 0x68,
                                 0x3D,
                                 0x31,
                                 0x35};

    // make a request
    nxp_common_request_processing_Expect();
    nxp_channel_get_nexus_id_ExpectAndReturn(src_nx_id);
    nxp_channel_network_send_ExpectAndReturn(request_bytes,
                                             sizeof(request_bytes),
                                             &src_nx_id,
                                             &dest_nx_id,
                                             false,
                                             0);
    TEST_ASSERT_EQUAL(
        NX_CHANNEL_ERROR_NONE,
        nx_channel_do_get_request(
            "test", &dest_nx_id, query, test_platform_get_handler, context));

    // process/send the request; arbitrary uptime
    nexus_channel_core_process(1);

    // handcraft a reply to route to the the client reply handler:
    // 51 45 E2 41 40 FF BF 62 74 68 14 FF
    // version 0x01, TKL 0x01, code 0x45 (2.05), MID 0xE242, token 0x40, payload
    // 0xBF62746814FF ({"th": 20} from battery resource)
    const uint8_t reply_bytes[12] = {
        0x51, 0x45, 0xE2, 0x42, 0x40, 0xFF, 0xBF, 0x62, 0x74, 0x68, 0x14, 0xFF};
    nxp_common_request_processing_Expect();
    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_NONE,
                      nx_channel_network_receive(
                          reply_bytes, sizeof(reply_bytes), &dest_nx_id));

    // custom callback to more easily examine the sent message and
    // confirm the payload is valid/expected.
    // Newer name is 'func_AddCallback`, but older convention used here
    // to allow older Ceedling versions in CI to run this test.
    test_platform_get_handler_ExpectAnyArgs();
    test_platform_get_handler_StubWithCallback(
        CALLBACK_test_nexus_oc_wrapper__nx_channel_do_get_post_request__callback_handler_check);
    nexus_channel_core_process(2);
}

void test_nexus_oc_wrapper__nx_channel_do_post_request__no_handler_set__fails(
    void)
{
    // We may tangentially trigger events in security manager tests, ignore
    nxp_channel_notify_event_Ignore();
    nxp_common_nv_read_IgnoreAndReturn(true);
    nxp_common_nv_write_IgnoreAndReturn(true);
    nxp_channel_random_value_IgnoreAndReturn(123456);
    nexus_channel_core_init();

    // register resource (server; replies)
    const struct nx_channel_resource_props pc_props = {
        .uri = "/test",
        .resource_type = "angaza.test",
        .rtr = 65000,
        .num_interfaces = 2,
        .if_masks = if_mask_arr,
        .get_handler = NULL,
        .get_secured = false,
        // arbitrary
        .post_handler = nexus_channel_res_payg_credit_get_handler,
        .post_secured = false};

    nx_channel_error reg_result = nx_channel_register_resource(&pc_props);

    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_NONE, reg_result);

    // make a request -- fails because we did not call
    // nx_channel_init_post_request
    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_UNSPECIFIED,
                      nx_channel_do_post_request());
}

void test_nexus_oc_wrapper__nx_channel_do_post_request__get_reply__success(void)
{
    // We may tangentially trigger events in security manager tests, ignore
    nxp_channel_notify_event_Ignore();
    nxp_common_nv_read_IgnoreAndReturn(true);
    nxp_common_nv_write_IgnoreAndReturn(true);
    nxp_channel_random_value_IgnoreAndReturn(123456);
    nexus_channel_core_init();

    struct nx_id src_nx_id = {0xFFFF, 0x12345678};
    struct nx_id dest_nx_id = {0xFFFF, 0x87654321};

    // register resource (server; replies)
    const struct nx_channel_resource_props pc_props = {
        .uri = "/test",
        .resource_type = "angaza.test",
        .rtr = 65000,
        .num_interfaces = 2,
        .if_masks = if_mask_arr,
        .get_handler = NULL,
        .get_secured = false,
        // arbitrary
        .post_handler = nexus_channel_res_payg_credit_get_handler,
        .post_secured = false};

    nx_channel_error reg_result = nx_channel_register_resource(&pc_props);

    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_NONE, reg_result);

    // arbitrary request context
    char* context = "context";

    // The request is (same token value due to random value mock above):
    // 51 01 E2 41 40 B4 74 65 73 74
    // version 0x01, TKL 0x01, code 0x01, MID 0xE241, token 0x40, option 0xB4
    // (uri-path, length 4), option value 0x74657374 (ASCII "test")
    uint8_t request_bytes[10] = {
        0x51, 0x01, 0xE2, 0x41, 0x40, 0xB4, 0x74, 0x65, 0x73, 0x74};

    // make a request
    TEST_ASSERT_EQUAL(
        NX_CHANNEL_ERROR_NONE,
        nx_channel_init_post_request(
            "test", &dest_nx_id, NULL, test_platform_post_handler, context));

    nxp_common_request_processing_Expect();
    nxp_channel_get_nexus_id_ExpectAndReturn(src_nx_id);
    nxp_channel_network_send_ExpectAndReturn(request_bytes,
                                             sizeof(request_bytes),
                                             &src_nx_id,
                                             &dest_nx_id,
                                             false,
                                             0);
    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_NONE, nx_channel_do_post_request());

    // process/send the request; arbitrary uptime
    nexus_channel_core_process(1);

    // handcraft a reply to route to the the client reply handler:
    // 51 45 E2 41 40 FF BF 62 74 68 14 FF
    // version 0x01, TKL 0x01, code 0x45 (2.05), MID 0xE242, token 0x40, payload
    // 0xBF62746814FF ({"th": 20} from battery resource)
    const uint8_t reply_bytes[12] = {
        0x51, 0x45, 0xE2, 0x42, 0x40, 0xFF, 0xBF, 0x62, 0x74, 0x68, 0x14, 0xFF};
    nxp_common_request_processing_Expect();
    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_NONE,
                      nx_channel_network_receive(
                          reply_bytes, sizeof(reply_bytes), &dest_nx_id));

    // custom callback to more easily examine the sent message and
    // confirm the payload is valid/expected.
    // Newer name is 'func_AddCallback`, but older convention used here
    // to allow older Ceedling versions in CI to run this test.
    test_platform_post_handler_ExpectAnyArgs();
    test_platform_post_handler_StubWithCallback(
        CALLBACK_test_nexus_oc_wrapper__nx_channel_do_get_post_request__callback_handler_check);
    nexus_channel_core_process(2);
}

#pragma GCC diagnostic pop
