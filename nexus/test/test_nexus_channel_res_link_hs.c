#include "include/nx_channel.h"
#include "messaging/coap/coap.h"
#include "messaging/coap/constants.h"
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
#include "oc/util/oc_etimer.h"
#include "oc/util/oc_mmem.h"
#include "oc/util/oc_process.h"
#include "oc/util/oc_timer.h"

#include "oc/port/oc_connectivity.h"
#include "util/oc_memb.h"
#include "utils/oc_list.h"
#include "utils/oc_uuid.h"

#include "src/internal_channel_config.h"
#include "src/nexus_channel_core.h"
#include "src/nexus_channel_om.h"
#include "src/nexus_channel_res_link_hs.h"
#include "src/nexus_channel_res_lm.h"
#include "src/nexus_channel_res_payg_credit.h"
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
// oc/api/oc_ri.c
extern bool oc_ri_invoke_coap_entity_handler(void* request,
                                             void* response,
                                             uint8_t* buffer,
                                             oc_endpoint_t* endpoint);

// from oc_ri.c
extern oc_event_callback_retval_t oc_ri_remove_client_cb(void* data);

// added for Nexus testing, in `oc_mmem.c`
extern void oc_nexus_testing_reinit_mmem_lists(void);
/******************************************************n
 * PRIVATE TYPES
 *******************************************************/

struct expect_rep
{
    oc_rep_value_type_t type;
    char* name;
    oc_rep_value value;
    bool received; // used to determine if we received all expected values
};

/********************************************************
 * PRIVATE DATA
 *******************************************************/
nexus_link_hs_accessory_t IDLE_HS_SERVER_STATE = {0};

const nexus_link_hs_accessory_t RECEIVED_CHALLENGE_HS_SERVER_STATE = {
    {0x05, 0x01, 0x39, 0xff, 0x55, 0x66, 0x77},
    {0},
    7,
    0,
    0,
    0,
    15, // started handshake 15 seconds ago
    LINK_HANDSHAKE_STATE_ACTIVE};

oc_endpoint_t FAKE_ENDPOINT = {0};

oc_endpoint_t MCAST_ENDPOINT = {
    NULL, // 'next'
    0, // device
    IPV6 | MULTICAST, // flags
    {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}}, // di
    {(oc_ipv6_addr_t){
        5683, // port
        {// mcast address 'all OCF devices'
         0xff,
         0x02,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0x01,
         0x58},
        2 // scope
    }},
    {{0}}, // addr_local (not used)
    0, // interface index (not used)
    0, // priority (not used)
    0, // ocf_version_t (unused)
};

oc_endpoint_t FAKE_ACCESSORY_ENDPOINT = {
    NULL, // 'next'
    0, // device
    IPV6, // flags
    {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}}, // di
    {(oc_ipv6_addr_t){
        5683, // port
        {// arbitrary link local address that represents a Nexus ID
         0xff,
         0x80,
         0,
         0,
         0,
         0,
         0,
         0,
         0xAE,
         0xD2,
         0x22,
         0xFF,
         0xFE,
         0x01,
         0xFB,
         0xFC},
        2 // scope
    }},
    {{0}}, // addr_local (not used)
    0, // interface index (not used)
    0, // priority (not used)
    0, // ocf_version_t (unused)
};

oc_endpoint_t FAKE_CONTROLLER_ENDPOINT = {
    NULL, // 'next'
    0, // device
    IPV6, // flags
    {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}}, // di
    {(oc_ipv6_addr_t){
        5683, // port
        {// arbitrary link local address that represents a Nexus ID
         0xff,
         0x80,
         0,
         0,
         0,
         0,
         0,
         0,
         0xAE,
         0xD2,
         0x22,
         0xFF,
         0xFE,
         0x01,
         0xA5,
         0x9B},
        2 // scope
    }},
    {{0}}, // addr_local (not used)
    0, // interface index (not used)
    0, // priority (not used)
    0, // ocf_version_t (unused)
};

oc_endpoint_t FAKE_CONTROLLER_ENDPOINT_B = {
    NULL, // 'next'
    0, // device
    IPV6, // flags
    {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}}, // di
    {(oc_ipv6_addr_t){
        5683, // port
        {// arbitrary link local address that represents a Nexus ID
         0xff,
         0x80,
         0,
         0,
         0,
         0,
         0,
         0,
         0xAE,
         0xD2,
         0x22,
         0xFF,
         0xFE,
         0xC1,
         0xA5,
         0xFC},
        2 // scope
    }},
    {{0}}, // addr_local (not used)
    0, // interface index (not used)
    0, // priority (not used)
    0, // ocf_version_t (unused)
};

// Global message that can be allocated and deallocated at start and end
// of tests regardless of failures
static oc_message_t* G_OC_MESSAGE = 0;
static oc_rep_t* G_OC_REP = NULL;
static oc_client_cb_t* G_OC_CLIENT_CB = 0;

/********************************************************
 * PRIVATE FUNCTIONS
 *******************************************************/
// pull in source file from IoTivity without changing its name
// https://github.com/ThrowTheSwitch/Ceedling/issues/113
TEST_FILE("oc/api/oc_server_api.c")
TEST_FILE("oc/api/oc_client_api.c")
TEST_FILE("oc/deps/tinycbor/cborencoder.c")
TEST_FILE("oc/deps/tinycbor/cborparser.c")

// Backing memory for parsing OC reps using `oc_parse_rep`
// Variables here must be static to persist between invocations of this
// function
void _initialize_oc_rep_pool(void)
{
    // Prepare an space for representing OC rep
    static char rep_objects_alloc[OC_MAX_NUM_REP_OBJECTS];
    static oc_rep_t rep_objects_pool[OC_MAX_NUM_REP_OBJECTS];
    memset(rep_objects_alloc, 0x00, OC_MAX_NUM_REP_OBJECTS * sizeof(char));
    memset(rep_objects_pool, 0x00, OC_MAX_NUM_REP_OBJECTS * sizeof(oc_rep_t));
    static struct oc_memb rep_objects;

    rep_objects.size = sizeof(oc_rep_t);
    rep_objects.num = OC_MAX_NUM_REP_OBJECTS;
    rep_objects.count = rep_objects_alloc;
    rep_objects.mem = (void*) rep_objects_pool;
    rep_objects.buffers_avail_cb = 0;

    oc_rep_set_pool(&rep_objects);
}

// Setup (called before any 'test_*' function is called, automatically)
void setUp(void)
{
    nxp_core_nv_read_IgnoreAndReturn(true);
    nxp_core_nv_write_IgnoreAndReturn(true);
    nxp_core_random_init_Ignore();
    nxp_core_random_value_IgnoreAndReturn(123456);
    oc_clock_init_Ignore();
    // register platform and device
    nexus_channel_core_init();

    // In tests, `nexus_channel_core_init` does not initialize channel
    // submodules,
    // so we can enable just this submodule manually
    nexus_channel_res_link_hs_init();
    // also need link manager to be initialized, since handshakes create
    // links
    nexus_channel_link_manager_init();

    // confirm that the initialized resource is valid/present
    // assumes device is at index '0'
    oc_resource_t* resource =
        oc_ri_get_app_resource_by_uri("h", 1, NEXUS_CHANNEL_NEXUS_DEVICE_ID);
    TEST_ASSERT_EQUAL_STRING_LEN("/h", resource->uri.ptr, 2);
    TEST_ASSERT_EQUAL_STRING_LEN("angaza.com.nexus.link.hs",
                                 resource->types.ptr,
                                 strlen("angaza.com.nexus.link.hs"));

    // will prepare CoAP engine to send/receive messages
    coap_init_engine();

    // must be deallocated at end of test
    G_OC_MESSAGE = oc_allocate_message();

    G_OC_REP = NULL;

    G_OC_CLIENT_CB = 0;
    OC_DBG("------ SETUP FINISHED, BEGINNING TEST ------");
}

// Teardown (called after any 'test_*' function is called, automatically)
void tearDown(void)
{
    OC_DBG("------ RUNNING TEARDOWN, END OF TEST ------");
    // Unref G_OC_MESSAGE and G_OC_REP here, because if a test fails and
    // deallocation is at the end of the test, the memory will not be
    // deallocated.

    // "G_OC_MESSAGE" is allocated on each test run in step
    oc_message_unref(G_OC_MESSAGE);
    // some tests *may* call oc_parse_rep, oc_free_rep handles this case
    // We null-check to make sure a test actually allocated G_OC_REP, otherwise
    // we won't be able to free it.
    if (G_OC_REP != NULL)
    {
        oc_free_rep(G_OC_REP);
    }

    coap_free_all_transactions();

    if (G_OC_CLIENT_CB != 0)
    {
        oc_ri_remove_client_cb(G_OC_CLIENT_CB);
    }

    nexus_channel_core_shutdown();

    // In some tests, we may leave certain lists with dangling or invalid
    // states if a test fails before IoTivity cleans up. We want to fully
    // erase the IoTivity memory, including linked lists, before moving to
    // the next test.
    oc_nexus_testing_reinit_mmem_lists();
}

void _internal_set_coap_headers(coap_packet_t* request_packet,
                                coap_message_type_t coap_type,
                                uint8_t coap_code)
{
    coap_udp_init_message(request_packet, coap_type, coap_code, 123);
    coap_set_header_uri_path(request_packet, "/h", strlen("/h"));
}

void test_res_link_server_process_idle_vs_active__process_seconds_returned_ok(
    void)
{
    _nexus_channel_res_link_hs_reset_server_state();
    uint32_t secs = nexus_channel_res_link_hs_process(0);
    TEST_ASSERT_EQUAL_INT(NEXUS_CORE_IDLE_TIME_BETWEEN_PROCESS_CALL_SECONDS,
                          secs);

    // Should expect to be called every 1s while a link handshake is in progress
    _nexus_channel_res_link_hs_set_server_state(
        &RECEIVED_CHALLENGE_HS_SERVER_STATE);
    secs = nexus_channel_res_link_hs_process(0);
    TEST_ASSERT_EQUAL_INT(1, secs);
}

void test_res_link_server_process_active_to_inactive__times_out(void)
{
    // Should expect to be called every 1s while a link handshake is in progress
    _nexus_channel_res_link_hs_set_server_state(
        &RECEIVED_CHALLENGE_HS_SERVER_STATE);
    uint32_t secs = nexus_channel_res_link_hs_process(0);
    TEST_ASSERT_EQUAL_INT(1, secs);

    // Timeout by elapsing more than timeout seconds
    nxp_channel_notify_event_Expect(NXP_CHANNEL_EVENT_LINK_HANDSHAKE_TIMED_OUT);
    secs = nexus_channel_res_link_hs_process(
        NEXUS_CHANNEL_LINK_HANDSHAKE_ACCESSORY_TIMEOUT_SECONDS + 1);
    TEST_ASSERT_EQUAL_INT(NEXUS_CORE_IDLE_TIME_BETWEEN_PROCESS_CALL_SECONDS,
                          secs);
}

void test_res_link_hs_server_get_response__default__cbor_data_model_correct(
    void)
{
    // internal state set to default/idle
    _nexus_channel_res_link_hs_reset_server_state();

    // Prepare buffers
    coap_packet_t request_packet = {0};
    coap_packet_t response_packet = {0};
    uint8_t RESP_BUFFER[2048] = {0};
    // Must be deallocated at end of test.
    // Allocate on `oc_incoming_buffers`
    TEST_ASSERT_NOT_EQUAL(NULL, G_OC_MESSAGE);

    // Prepare a GET message
    _internal_set_coap_headers(&request_packet, COAP_TYPE_NON, COAP_GET);

    G_OC_MESSAGE->length =
        coap_serialize_message(&request_packet, G_OC_MESSAGE->data);

    OC_DBG("Requesting GET to '/h' URI");

    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_ENDPOINT);
    TEST_ASSERT_TRUE(handled);

    PRINT("Raw CBOR Payload bytes follow (1):\n");
    // Print CBOR payload for demonstration
    for (uint8_t i = 0; i < response_packet.payload_len; ++i)
    {
        PRINT("%02x ", (uint8_t) * (response_packet.payload + i));
    }
    PRINT("\n");

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(CONTENT_2_05, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(42, response_packet.payload_len);

    _initialize_oc_rep_pool();
    oc_parse_rep(response_packet.payload, // payload,
                 response_packet.payload_len,
                 &G_OC_REP);

    // Local rep to iterate through `iotivity_rep` without modifying it
    // This is necessary so that when we want to deallocate `G_OC_REP`,
    // it is pointing to the first element in the representation (since
    // we will modify 'new_rep' here on each loop iteration)
    oc_rep_t new_rep;
    memcpy(&new_rep, G_OC_REP, sizeof(oc_rep_t));
    oc_rep_t* loop_rep_ptr = &new_rep;

    // Define expected representation
    int supported_link_security_modes[1] = {0};
    int supported_challenge_modes[1] = {0};
    struct expect_rep idle_rep_no_baseline[] = {
        {OC_REP_BYTE_STRING,
         (char*) CHAL_DATA_SHORT_PROP_NAME,
         {.string = {0}},
         false},
        {OC_REP_BYTE_STRING,
         (char*) RESP_DATA_SHORT_PROP_NAME,
         {.string = {0}},
         false},
        {OC_REP_INT,
         (char*) CHAL_MODE_SHORT_PROP_NAME,
         {.integer = IDLE_HS_SERVER_STATE.chal_mode},
         false},
        {OC_REP_INT,
         (char*) LINK_SEC_MODE_SHORT_PROP_NAME,
         {.integer = IDLE_HS_SERVER_STATE.link_security_mode},
         false},
        {OC_REP_INT,
         (char*) STATE_SHORT_PROP_NAME,
         {.integer = IDLE_HS_SERVER_STATE.state},
         false},
        {OC_REP_INT, (char*) TIME_SINCE_INIT_SHORT_PROP_NAME, {0}, false},
        {OC_REP_INT,
         (char*) TIMEOUT_CONFIGURED_SHORT_PROP_NAME,
         {.integer = NEXUS_CHANNEL_LINK_HANDSHAKE_ACCESSORY_TIMEOUT_SECONDS},
         false},
        {OC_REP_INT_ARRAY,
         (char*) SUPPORTED_LINK_SECURITY_MODES_SHORT_PROP_NAME,
         {.array = {NULL, 1ul, (void*) supported_link_security_modes}},
         false},
        {OC_REP_INT_ARRAY,
         (char*) SUPPORTED_CHALLENGE_MODES_SHORT_PROP_NAME,
         {.array = {NULL, 1ul, (void*) supported_challenge_modes}},
         false},
    };

    // Iterate through the resulting response payload, checking for
    // consistency

    while (loop_rep_ptr != NULL)
    {
        oc_rep_value val = loop_rep_ptr->value;
        handled = false;
        OC_DBG("name is %s", oc_string(loop_rep_ptr->name));

        for (uint8_t i = 0;
             i < sizeof(idle_rep_no_baseline) / sizeof(idle_rep_no_baseline[0]);
             ++i)
        {
            struct expect_rep* rep = &idle_rep_no_baseline[i];

            if (strncmp(rep->name, oc_string(loop_rep_ptr->name), 2) == 0)
            {
                OC_DBG("type is %d", rep->type);
                TEST_ASSERT_EQUAL(rep->type, loop_rep_ptr->type);
                if (rep->type == OC_REP_INT || rep->type == OC_REP_BOOL)
                {
                    TEST_ASSERT_EQUAL_INT(rep->value.integer, val.integer);
                }
                else if (rep->type == OC_REP_INT_ARRAY)
                {
                    int64_t* val_array = oc_int_array(val.array);
                    int64_t* expected_array = oc_int_array(rep->value.array);
                    // loop through our expected value array
                    for (uint32_t j = 0; j < rep->value.array.size - 1; ++j)
                    {
                        TEST_ASSERT_EQUAL_INT(expected_array[j], val_array[j]);
                    }
                }
                else if (rep->type == OC_REP_NIL)
                {
                    // Do nothing
                }
                else if (rep->type == OC_REP_BYTE_STRING)
                {
                    const uint8_t expected_length =
                        (uint8_t) oc_string_len(rep->value.string);
                    if (expected_length != 0)
                    {
                        // IoTivity decodes bytetring payloads of 0 length as
                        // size 1, null terminating byte.
                        TEST_ASSERT_EQUAL(expected_length + 1,
                                          oc_string_len(val.string));
                    }
                    else
                    {
                        TEST_ASSERT_EQUAL(0, oc_string_len(val.string));
                    }
                    uint8_t* expected_data =
                        oc_cast(rep->value.string, uint8_t);
                    uint8_t* received_data = oc_cast(val.string, uint8_t);
                    if (expected_length > 0)
                    {
                        TEST_ASSERT_EQUAL_UINT8_ARRAY(
                            expected_data, received_data, expected_length);
                    }
                    OC_DBG("here");
                    // TEST_FAIL_MESSAGE("Unhandled rep");
                }

                // each rep occurs once in the response payload
                TEST_ASSERT_FALSE(rep->received);
                rep->received = true;
                handled = true;
            }
        }
        // Otherwise, data is unexpected in the response
        TEST_ASSERT_TRUE(handled);
        loop_rep_ptr = loop_rep_ptr->next;
    }

    // now, confirm all expected reps were in the response payload
    for (uint8_t i = 0;
         i < sizeof(idle_rep_no_baseline) / sizeof(idle_rep_no_baseline[0]);
         ++i)
    {
        OC_DBG("name? %s", idle_rep_no_baseline[i].name);
        OC_DBG("Type? %d", idle_rep_no_baseline[i].type);
        TEST_ASSERT_TRUE(idle_rep_no_baseline[i].received);
    }
}

void test_res_link_hs_server_get_response__default_with_baseline__cbor_data_model_correct(
    void)
{
    // internal state set to default/idle
    _nexus_channel_res_link_hs_reset_server_state();

    // Prepare buffers
    coap_packet_t request_packet = {0};
    coap_packet_t response_packet = {0};
    uint8_t RESP_BUFFER[2048] = {0};
    // Must be deallocated at end of test.
    // Allocate on `oc_incoming_buffers`
    TEST_ASSERT_NOT_EQUAL(NULL, G_OC_MESSAGE);

    // Prepare a GET message with baseline interface
    _internal_set_coap_headers(&request_packet, COAP_TYPE_NON, COAP_GET);

    // set baseline query
    char* baseline_query_str = "if=oic.if.baseline\0";
    coap_set_header_uri_query(&request_packet, baseline_query_str);

    G_OC_MESSAGE->length =
        coap_serialize_message(&request_packet, G_OC_MESSAGE->data);

    // sanity check that header was set
    TEST_ASSERT_EQUAL_STRING_LEN(baseline_query_str,
                                 request_packet.uri_query,
                                 strlen(baseline_query_str));
    TEST_ASSERT_EQUAL_INT(strlen(baseline_query_str),
                          request_packet.uri_query_len);

    OC_DBG("Requesting GET to '/h' URI with baseline");

    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_ENDPOINT);
    TEST_ASSERT_TRUE(handled);

    PRINT("Raw CBOR Payload bytes follow (1):\n");
    // Print CBOR payload for demonstration
    // {"rt": ["angaza.com.nexus.link.hs"], "if": ["oic.if.rw",
    // "oic.if.baseline"], "cD": h'', "rD": h'', "cM": 0, "lS": 0, "st": 0,
    // "tI": 0, "tT": 300, "sL": [0], "sC": [0]}
    //
    uint8_t expected_payload_bytes[104] = {
        0xbf, 0x62, 0x72, 0x74, 0x9f, 0x78, 0x18, 0x61, 0x6e, 0x67, 0x61, 0x7a,
        0x61, 0x2e, 0x63, 0x6f, 0x6d, 0x2e, 0x6e, 0x65, 0x78, 0x75, 0x73, 0x2e,
        0x6c, 0x69, 0x6e, 0x6b, 0x2e, 0x68, 0x73, 0xff, 0x62, 0x69, 0x66, 0x9f,
        0x69, 0x6f, 0x69, 0x63, 0x2e, 0x69, 0x66, 0x2e, 0x72, 0x77, 0x6f, 0x6f,
        0x69, 0x63, 0x2e, 0x69, 0x66, 0x2e, 0x62, 0x61, 0x73, 0x65, 0x6c, 0x69,
        0x6e, 0x65, 0xff, 0x62, 0x63, 0x44, 0x40, 0x62, 0x72, 0x44, 0x40, 0x62,
        0x63, 0x4d, 0x0,  0x62, 0x6c, 0x53, 0x0,  0x62, 0x73, 0x74, 0x0,  0x62,
        0x74, 0x49, 0x0,  0x62, 0x74, 0x54, 0x19, 0x1,  0x2c, 0x62, 0x73, 0x4c,
        0x81, 0x0,  0x62, 0x73, 0x43, 0x81, 0x0,  0xff};
    for (uint8_t i = 0; i < response_packet.payload_len; ++i)
    {
        PRINT("%02x ", (uint8_t) * (response_packet.payload + i));
        TEST_ASSERT_EQUAL_UINT(expected_payload_bytes[i],
                               (uint8_t) * (response_packet.payload + i));
    }
    PRINT("\n");

    // Check response code and content
    // Note: Parsing a message with baseline content does not work
    // currently, but we can confirm the 111 bytes contain baseline content (rt,
    // if)
    TEST_ASSERT_EQUAL_UINT(CONTENT_2_05, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(104, response_packet.payload_len);

    _initialize_oc_rep_pool();
    int success = oc_parse_rep(response_packet.payload, // payload,
                               response_packet.payload_len,
                               &G_OC_REP);
    TEST_ASSERT_EQUAL(0, success);
}

void test_res_link_hs_server_get_response__simulated_challenge_received__cbor_data_model_correct(
    void)
{
    // Set internal resource state
    _nexus_channel_res_link_hs_set_server_state(
        &RECEIVED_CHALLENGE_HS_SERVER_STATE);

    // Prepare buffers
    coap_packet_t request_packet = {0};
    coap_packet_t response_packet = {0};
    uint8_t RESP_BUFFER[2048] = {0};
    // Must be deallocated at end of test.
    // Allocated on `oc_incoming_buffers` by setUp
    TEST_ASSERT_NOT_EQUAL(NULL, G_OC_MESSAGE);

    // Prepare a GET message
    _internal_set_coap_headers(&request_packet, COAP_TYPE_NON, COAP_GET);

    G_OC_MESSAGE->length =
        coap_serialize_message(&request_packet, G_OC_MESSAGE->data);

    OC_DBG("Requesting GET to '/h' URI");

    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_ENDPOINT);
    TEST_ASSERT_TRUE(handled);

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(CONTENT_2_05, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(49, response_packet.payload_len);

    PRINT("Raw CBOR Payload bytes follow (1):\n");
    // Print CBOR payload for demonstration
    for (uint8_t i = 0; i < response_packet.payload_len; ++i)
    {
        PRINT("%02x ", (uint8_t) * (response_packet.payload + i));
    }

    _initialize_oc_rep_pool();
    int error = oc_parse_rep(response_packet.payload, // payload,
                             response_packet.payload_len,
                             &G_OC_REP);

    TEST_ASSERT_EQUAL_INT(0, error);

    // Local rep to iterate through `G_OC_REP` without modifying it
    // This is necessary so that when we want to deallocate `G_OC_REP`,
    // it is pointing to the first element in the representation.
    oc_rep_t new_rep;
    memcpy(&new_rep, G_OC_REP, sizeof(oc_rep_t));
    oc_rep_t* loop_rep_ptr = &new_rep;

    // Define expected representation
    int supported_link_security_modes[1] = {0};
    int supported_challenge_modes[1] = {0};
    struct expect_rep expect_rep_received_challenge_hs_server[] = {
        {OC_REP_BYTE_STRING,
         (char*) CHAL_DATA_SHORT_PROP_NAME,
         {.string = {NULL,
                     RECEIVED_CHALLENGE_HS_SERVER_STATE.chal_data_len,
                     (void*) RECEIVED_CHALLENGE_HS_SERVER_STATE.chal_data}},
         false}, // value ignored for 'nil'
        {OC_REP_BYTE_STRING,
         (char*) RESP_DATA_SHORT_PROP_NAME,
         {.string = {NULL, 0, 0}},
         false}, // value ignored for 'nil'
        {OC_REP_INT,
         (char*) CHAL_MODE_SHORT_PROP_NAME,
         {.integer = RECEIVED_CHALLENGE_HS_SERVER_STATE.chal_mode},
         false},
        {OC_REP_INT,
         (char*) LINK_SEC_MODE_SHORT_PROP_NAME,
         {.integer = RECEIVED_CHALLENGE_HS_SERVER_STATE.link_security_mode},
         false},
        {OC_REP_INT,
         (char*) STATE_SHORT_PROP_NAME,
         {.integer = RECEIVED_CHALLENGE_HS_SERVER_STATE.state},
         false},
        {OC_REP_INT,
         (char*) TIME_SINCE_INIT_SHORT_PROP_NAME,
         {.integer = RECEIVED_CHALLENGE_HS_SERVER_STATE.seconds_since_init},
         false},
        {OC_REP_INT,
         (char*) TIMEOUT_CONFIGURED_SHORT_PROP_NAME,
         {.integer = NEXUS_CHANNEL_LINK_HANDSHAKE_ACCESSORY_TIMEOUT_SECONDS},
         false},
        {OC_REP_INT_ARRAY,
         (char*) SUPPORTED_LINK_SECURITY_MODES_SHORT_PROP_NAME,
         {.array = {NULL, 1ul, (void*) supported_link_security_modes}},
         false},
        {OC_REP_INT_ARRAY,
         (char*) SUPPORTED_CHALLENGE_MODES_SHORT_PROP_NAME,
         {.array = {NULL, 1ul, (void*) supported_challenge_modes}},
         false},
    };

    // Iterate through the resulting response payload, checking for
    // consistency
    while (loop_rep_ptr != NULL && loop_rep_ptr->name.ptr != NULL)
    {
        oc_rep_value val = loop_rep_ptr->value;
        handled = false;

        PRINT("Name of the current item? %s\n", oc_string(loop_rep_ptr->name));

        // loop through all expected representation items and and match
        // against the actual parsed rep
        for (uint8_t i = 0;
             i < sizeof(expect_rep_received_challenge_hs_server) /
                     sizeof(expect_rep_received_challenge_hs_server[0]);
             ++i)
        {
            struct expect_rep* rep =
                &expect_rep_received_challenge_hs_server[i];

            if (strncmp(rep->name, oc_string(loop_rep_ptr->name), 2) == 0)
            {
                PRINT("expected element name is %s, ", rep->name);
                PRINT("type is %d\n", rep->type);
                TEST_ASSERT_EQUAL(rep->type, loop_rep_ptr->type);
                if (rep->type == OC_REP_INT || rep->type == OC_REP_BOOL)
                {
                    TEST_ASSERT_EQUAL_INT(rep->value.integer, val.integer);
                }
                else if (rep->type == OC_REP_INT_ARRAY)
                {
                    int64_t* val_array = oc_int_array(val.array);
                    int64_t* expected_array = oc_int_array(rep->value.array);
                    // loop through our expected value array
                    for (uint32_t j = 0; j < rep->value.array.size - 1; ++j)
                    {
                        // OC_DBG("expected is %i actual is %i",
                        // expected_array[j], val_array[j]);
                        TEST_ASSERT_EQUAL_INT(expected_array[j], val_array[j]);
                    }
                }
                else if (rep->type == OC_REP_NIL)
                {
                    // do nothing
                }
                else if (rep->type == OC_REP_BYTE_STRING)
                {
                    const uint8_t expected_length =
                        (uint8_t) oc_string_len(rep->value.string);
                    PRINT("Expecting length of %d", expected_length);
                    if (expected_length != 0)
                    {
                        // IoTivity decodes bytetring payloads of 0 length as
                        // size 1, null terminating byte.
                        TEST_ASSERT_EQUAL(expected_length + 1,
                                          oc_string_len(val.string));
                    }
                    else
                    {
                        TEST_ASSERT_EQUAL(0, oc_string_len(val.string));
                    }
                    uint8_t* expected_data =
                        oc_cast(rep->value.string, uint8_t);
                    uint8_t* received_data = oc_cast(val.string, uint8_t);
                    if (expected_length > 0)
                    {
                        TEST_ASSERT_EQUAL_UINT8_ARRAY(
                            expected_data, received_data, expected_length);
                    }
                    // TEST_FAIL_MESSAGE("Unhandled rep");
                }
                else
                {
                    TEST_FAIL_MESSAGE("Unhandled rep");
                }

                // each rep occurs once in the response payload
                TEST_ASSERT_FALSE(rep->received);
                rep->received = true;
                handled = true;
            }
        }
        // Otherwise, data is unexpected in the response
        TEST_ASSERT_TRUE(handled);
        loop_rep_ptr = loop_rep_ptr->next;
    }
    // now, confirm all expected reps were in the response payload
    for (uint8_t i = 0;
         i < sizeof(expect_rep_received_challenge_hs_server) /
                 sizeof(expect_rep_received_challenge_hs_server[0]);
         ++i)
    {
        OC_DBG("name? %s", expect_rep_received_challenge_hs_server[i].name);
        OC_DBG("Type? %d", expect_rep_received_challenge_hs_server[i].type);
        TEST_ASSERT_TRUE(expect_rep_received_challenge_hs_server[i].received);
    }
}

void test_res_link_hs_server_post_response__unknown_payload_received__error_400_returned(
    void)
{
    // assume accessory is in idle state, waiting for handshake
    _nexus_channel_res_link_hs_reset_server_state();

    // Prepare buffers
    coap_packet_t request_packet = {0};
    coap_packet_t response_packet = {0};
    uint8_t RESP_BUFFER[2048] = {0};
    // Must be deallocated at end of test.
    // Allocated on `oc_incoming_buffers` by setUp
    TEST_ASSERT_NOT_EQUAL(NULL, G_OC_MESSAGE);

    // {"sL": [0], "sC": [0]}
    // Invalid key types for this endpoint - not expecting arrays
    //
    uint8_t request_payload_bytes[] = {
        0xbf, 0x62, 0x73, 0x4c, 0x81, 0x00, 0x62, 0x74, 0x43, 0x81, 0x00, 0xFF};

    // Prepare a valid POST message with a supported challenge
    _internal_set_coap_headers(&request_packet, COAP_TYPE_NON, COAP_POST);
    coap_set_header_content_format(&request_packet, APPLICATION_VND_OCF_CBOR);
    coap_set_payload(
        &request_packet, request_payload_bytes, sizeof(request_payload_bytes));

    G_OC_MESSAGE->length =
        coap_serialize_message(&request_packet, G_OC_MESSAGE->data);

    OC_DBG("Requesting POST to '/h' URI");

    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_CONTROLLER_ENDPOINT);
    TEST_ASSERT_TRUE(handled);

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(BAD_REQUEST_4_00, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(0, response_packet.payload_len);
}

void test_res_link_hs_server_post_response__unsupported_challenge_mode_received__error_400_returned(
    void)
{
    // assume accessory is in idle state, waiting for handshake
    _nexus_channel_res_link_hs_reset_server_state();

    // Prepare buffers
    coap_packet_t request_packet = {0};
    coap_packet_t response_packet = {0};
    uint8_t RESP_BUFFER[2048] = {0};
    // Must be deallocated at end of test.
    // Allocated on `oc_incoming_buffers` by setUp
    TEST_ASSERT_NOT_EQUAL(NULL, G_OC_MESSAGE);

    // {"cD": h'0102030405', "cM": 500, "lS": 0}
    // properties are valid, but challenge mode 500 does not exist
    //
    uint8_t request_payload_bytes[] = {0xA3, 0x62, 0x63, 0x44, 0x45, 0x01, 0x02,
                                       0x03, 0x04, 0x05, 0x62, 0x63, 0x4d, 0x19,
                                       0x01, 0xf4, 0x62, 0x6c, 0x53, 0x00};

    // Prepare a valid POST message with a supported challenge
    _internal_set_coap_headers(&request_packet, COAP_TYPE_NON, COAP_POST);
    coap_set_header_content_format(&request_packet, APPLICATION_VND_OCF_CBOR);
    coap_set_payload(
        &request_packet, request_payload_bytes, sizeof(request_payload_bytes));

    G_OC_MESSAGE->length =
        coap_serialize_message(&request_packet, G_OC_MESSAGE->data);

    OC_DBG("Requesting POST to '/h' URI");

    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_CONTROLLER_ENDPOINT);
    TEST_ASSERT_TRUE(handled);

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(BAD_REQUEST_4_00, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(0, response_packet.payload_len);
}

void test_res_link_hs_server_post_response__unsupported_security_mode_received__error_400_returned(
    void)
{
    // assume accessory is in idle state, waiting for handshake
    _nexus_channel_res_link_hs_reset_server_state();

    // Prepare buffers
    coap_packet_t request_packet = {0};
    coap_packet_t response_packet = {0};
    uint8_t RESP_BUFFER[2048] = {0};
    // Must be deallocated at end of test.
    // Allocated on `oc_incoming_buffers` by setUp
    TEST_ASSERT_NOT_EQUAL(NULL, G_OC_MESSAGE);

    // {"cD": h'0102030405', "cM": 0, "lS": 500}
    // properties are valid, but link security mode 500 does not exist
    //
    uint8_t request_payload_bytes[] = {0xA3, 0x62, 0x63, 0x44, 0x45, 0x01, 0x02,
                                       0x03, 0x04, 0x05, 0x62, 0x63, 0x4d, 0x00,
                                       0x62, 0x6c, 0x53, 0x19, 0x01, 0xf4};

    // Prepare a valid POST message with a supported challenge
    _internal_set_coap_headers(&request_packet, COAP_TYPE_NON, COAP_POST);
    coap_set_header_content_format(&request_packet, APPLICATION_VND_OCF_CBOR);
    coap_set_payload(
        &request_packet, request_payload_bytes, sizeof(request_payload_bytes));

    G_OC_MESSAGE->length =
        coap_serialize_message(&request_packet, G_OC_MESSAGE->data);

    OC_DBG("Requesting POST to '/h' URI");

    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_CONTROLLER_ENDPOINT);
    TEST_ASSERT_TRUE(handled);

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(BAD_REQUEST_4_00, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(0, response_packet.payload_len);
}

void test_res_link_hs_server_post_response__missing_a_payload_field__error_400_returned(
    void)
{
    // assume accessory is in idle state, waiting for handshake
    _nexus_channel_res_link_hs_reset_server_state();

    // Prepare buffers
    coap_packet_t request_packet = {0};
    coap_packet_t response_packet = {0};
    uint8_t RESP_BUFFER[2048] = {0};
    // Must be deallocated at end of test.
    // Allocated on `oc_incoming_buffers` by setUp
    TEST_ASSERT_NOT_EQUAL(NULL, G_OC_MESSAGE);

    // {"cD": h'0102030405', "cM": 0}
    // properties are valid, but missing lS
    //
    uint8_t request_payload_bytes[] = {0xA2,
                                       0x62,
                                       0x63,
                                       0x44,
                                       0x45,
                                       0x01,
                                       0x02,
                                       0x03,
                                       0x04,
                                       0x05,
                                       0x62,
                                       0x63,
                                       0x4d,
                                       0x00};

    // Prepare a valid POST message with a supported challenge
    _internal_set_coap_headers(&request_packet, COAP_TYPE_NON, COAP_POST);
    coap_set_header_content_format(&request_packet, APPLICATION_VND_OCF_CBOR);
    coap_set_payload(
        &request_packet, request_payload_bytes, sizeof(request_payload_bytes));

    G_OC_MESSAGE->length =
        coap_serialize_message(&request_packet, G_OC_MESSAGE->data);

    OC_DBG("Requesting POST to '/h' URI");

    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_CONTROLLER_ENDPOINT);
    TEST_ASSERT_TRUE(handled);

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(BAD_REQUEST_4_00, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(0, response_packet.payload_len);
}

void test_res_link_hs_server_post_response__challenge_data_too_large__error_400_returned(
    void)
{
    // assume accessory is in idle state, waiting for handshake
    _nexus_channel_res_link_hs_reset_server_state();

    // Prepare buffers
    coap_packet_t request_packet = {0};
    coap_packet_t response_packet = {0};
    uint8_t RESP_BUFFER[2048] = {0};
    // Must be deallocated at end of test.
    // Allocated on `oc_incoming_buffers` by setUp
    TEST_ASSERT_NOT_EQUAL(NULL, G_OC_MESSAGE);

    // {"cD": h'0102030405AABBCCDDEEFFA1B1C1D1E1F1550044000102030405AABB0ADB',
    // "cM": 0, "lS": 0}
    // properties are valid, but challenge data length 30 exceeds limit
    // (This test may change in the future to accomodate longer challenge data)
    //
    uint8_t request_payload_bytes[] = {
        0xA3, 0x62, 0x63, 0x44, 0x58, 0x1E, 0x01, 0x02, 0x03, 0x04, 0x05,
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0xA1, 0xB1, 0xC1, 0xD1, 0xE1,
        0xF1, 0x55, 0x00, 0x44, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0xAA,
        0xBB, 0x0A, 0xDB, 0x62, 0x63, 0x4d, 0x00, 0x62, 0x6c, 0x53, 0x00};

    // Prepare a valid POST message with a supported challenge
    _internal_set_coap_headers(&request_packet, COAP_TYPE_NON, COAP_POST);
    coap_set_header_content_format(&request_packet, APPLICATION_VND_OCF_CBOR);
    coap_set_payload(
        &request_packet, request_payload_bytes, sizeof(request_payload_bytes));

    G_OC_MESSAGE->length =
        coap_serialize_message(&request_packet, G_OC_MESSAGE->data);

    OC_DBG("Requesting POST to '/h' URI");

    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_CONTROLLER_ENDPOINT);
    TEST_ASSERT_TRUE(handled);

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(BAD_REQUEST_4_00, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(0, response_packet.payload_len);
}

void test_res_link_hs_server_post_response__challenge_data_invalid_type__error_400_returned(
    void)
{
    // assume accessory is in idle state, waiting for handshake
    _nexus_channel_res_link_hs_reset_server_state();

    // Prepare buffers
    coap_packet_t request_packet = {0};
    coap_packet_t response_packet = {0};
    uint8_t RESP_BUFFER[2048] = {0};
    // Must be deallocated at end of test.
    // Allocated on `oc_incoming_buffers` by setUp
    TEST_ASSERT_NOT_EQUAL(NULL, G_OC_MESSAGE);

    // {"cD": 1234567890, "cM": 0, "lS": 0}
    //  Challenge data should be a bytestring, not an unsigned integer
    uint8_t request_payload_bytes[] = {0xA3,
                                       0x62,
                                       0x63,
                                       0x44,
                                       0x1A,
                                       0x49,
                                       0x96,
                                       0x02,
                                       0xd2,
                                       0x62,
                                       0x63,
                                       0x4d,
                                       0x00,
                                       0x62,
                                       0x6c,
                                       0x53,
                                       0x00};

    // Prepare a valid POST message with a supported challenge
    _internal_set_coap_headers(&request_packet, COAP_TYPE_NON, COAP_POST);
    coap_set_header_content_format(&request_packet, APPLICATION_VND_OCF_CBOR);
    coap_set_payload(
        &request_packet, request_payload_bytes, sizeof(request_payload_bytes));

    G_OC_MESSAGE->length =
        coap_serialize_message(&request_packet, G_OC_MESSAGE->data);

    OC_DBG("Requesting POST to '/h' URI");

    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_CONTROLLER_ENDPOINT);
    TEST_ASSERT_TRUE(handled);

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(BAD_REQUEST_4_00, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(0, response_packet.payload_len);
}

void test_res_link_hs_server_post_response__challenge_data_invalid_data_length_for_mode__error_400_returned(
    void)
{
    // assume accessory is in idle state, waiting for handshake
    _nexus_channel_res_link_hs_reset_server_state();

    // Prepare buffers
    coap_packet_t request_packet = {0};
    coap_packet_t response_packet = {0};
    uint8_t RESP_BUFFER[2048] = {0};
    // Must be deallocated at end of test.
    // Allocated on `oc_incoming_buffers` by setUp
    TEST_ASSERT_NOT_EQUAL(NULL, G_OC_MESSAGE);

    // {"cD": h'0102030405', "cM": 0, "lS": 0, "badkey": 0}
    // Invalid length for challenge data (5 not supported for selected mode)
    uint8_t request_payload_bytes[] = {0xA4, 0x62, 0x63, 0x44, 0x45, 0x01, 0x02,
                                       0x03, 0x04, 0x05, 0x62, 0x63, 0x4d, 0x00,
                                       0x62, 0x6c, 0x53, 0x00, 0x66, 0x62, 0x61,
                                       0x64, 0x6B, 0x65, 0x79, 0x00};

    // Prepare a valid POST message with a supported challenge
    _internal_set_coap_headers(&request_packet, COAP_TYPE_NON, COAP_POST);
    coap_set_header_content_format(&request_packet, APPLICATION_VND_OCF_CBOR);
    coap_set_payload(
        &request_packet, request_payload_bytes, sizeof(request_payload_bytes));

    G_OC_MESSAGE->length =
        coap_serialize_message(&request_packet, G_OC_MESSAGE->data);

    OC_DBG("Requesting POST to '/h' URI");

    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_CONTROLLER_ENDPOINT);
    TEST_ASSERT_TRUE(handled);

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(BAD_REQUEST_4_00, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(0, response_packet.payload_len);
}

void test_res_link_hs_server_post_response__extra_invalid_int_key__error_400_returned(
    void)
{
    // assume accessory is in idle state, waiting for handshake
    _nexus_channel_res_link_hs_reset_server_state();

    // Prepare buffers
    coap_packet_t request_packet = {0};
    coap_packet_t response_packet = {0};
    uint8_t RESP_BUFFER[2048] = {0};
    // Must be deallocated at end of test.
    // Allocated on `oc_incoming_buffers` by setUp
    TEST_ASSERT_NOT_EQUAL(NULL, G_OC_MESSAGE);

    // {"cD": h'0102030405', "cM": 0, "lS": 0} // expecting length 8
    //  Extra unexpected integer key in payload.
    uint8_t request_payload_bytes[] = {0xA4, 0x62, 0x63, 0x44, 0x45, 0x01, 0x02,
                                       0x03, 0x04, 0x05, 0x62, 0x63, 0x4d, 0x00,
                                       0x62, 0x6c, 0x53, 0x00, 0x66, 0x62, 0x61,
                                       0x64, 0x6B, 0x65, 0x79, 0x00};

    // Prepare a valid POST message with a supported challenge
    _internal_set_coap_headers(&request_packet, COAP_TYPE_NON, COAP_POST);
    coap_set_header_content_format(&request_packet, APPLICATION_VND_OCF_CBOR);
    coap_set_payload(
        &request_packet, request_payload_bytes, sizeof(request_payload_bytes));

    G_OC_MESSAGE->length =
        coap_serialize_message(&request_packet, G_OC_MESSAGE->data);

    OC_DBG("Requesting POST to '/h' URI");

    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_CONTROLLER_ENDPOINT);
    TEST_ASSERT_TRUE(handled);

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(BAD_REQUEST_4_00, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(0, response_packet.payload_len);
}

void test_res_link_hs_server_post_response__extra_invalid_bytestring_key__error_400_returned(
    void)
{
    // assume accessory is in idle state, waiting for handshake
    _nexus_channel_res_link_hs_reset_server_state();

    // Prepare buffers
    coap_packet_t request_packet = {0};
    coap_packet_t response_packet = {0};
    uint8_t RESP_BUFFER[2048] = {0};
    // Must be deallocated at end of test.
    // Allocated on `oc_incoming_buffers` by setUp
    TEST_ASSERT_NOT_EQUAL(NULL, G_OC_MESSAGE);

    //  {"cD": h'0102030405', "cM": 0, "lS": 0, "badkey": h'00'}
    //  Extra unexpected bytestring key in payload.
    uint8_t request_payload_bytes[] = {0xA4, 0x62, 0x63, 0x44, 0x45, 0x01, 0x02,
                                       0x03, 0x04, 0x05, 0x62, 0x63, 0x4d, 0x00,
                                       0x62, 0x6c, 0x53, 0x00, 0x66, 0x62, 0x61,
                                       0x64, 0x6B, 0x65, 0x79, 0x41, 0x00};

    // Prepare a valid POST message with a supported challenge
    _internal_set_coap_headers(&request_packet, COAP_TYPE_NON, COAP_POST);
    coap_set_header_content_format(&request_packet, APPLICATION_VND_OCF_CBOR);
    coap_set_payload(
        &request_packet, request_payload_bytes, sizeof(request_payload_bytes));

    G_OC_MESSAGE->length =
        coap_serialize_message(&request_packet, G_OC_MESSAGE->data);

    OC_DBG("Requesting POST to '/h' URI");

    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_CONTROLLER_ENDPOINT);
    TEST_ASSERT_TRUE(handled);

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(BAD_REQUEST_4_00, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(0, response_packet.payload_len);
}

void test_res_link_hs_server_post_response__empty_payload_sent__error_400_returned(
    void)
{
    // assume accessory is in idle state, waiting for handshake
    _nexus_channel_res_link_hs_reset_server_state();

    // Prepare buffers
    coap_packet_t request_packet = {0};
    coap_packet_t response_packet = {0};
    uint8_t RESP_BUFFER[2048] = {0};
    // Must be deallocated at end of test.
    // Allocated on `oc_incoming_buffers` by setUp
    TEST_ASSERT_NOT_EQUAL(NULL, G_OC_MESSAGE);

    // Prepare a valid POST message with a supported challenge
    _internal_set_coap_headers(&request_packet, COAP_TYPE_NON, COAP_POST);
    coap_set_header_content_format(&request_packet, APPLICATION_VND_OCF_CBOR);
    // not 'set payload' call, no payload defined for message

    G_OC_MESSAGE->length =
        coap_serialize_message(&request_packet, G_OC_MESSAGE->data);

    OC_DBG("Requesting POST to '/h' URI");

    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_CONTROLLER_ENDPOINT);
    TEST_ASSERT_TRUE(handled);

    TEST_ASSERT_EQUAL_UINT(BAD_REQUEST_4_00, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(0, response_packet.payload_len);
}

void test_res_link_hs_server_post_response__supported_valid_challenge_mode0_received__accessory_link_created_valid_response(
    void)
{
    // assume accessory is in idle state, waiting for handshake
    _nexus_channel_res_link_hs_reset_server_state();

    // Prepare buffers
    coap_packet_t request_packet = {0};
    coap_packet_t response_packet = {0};
    uint8_t RESP_BUFFER[2048] = {0};
    // Must be deallocated at end of test.
    // Allocated on `oc_incoming_buffers` by setUp
    TEST_ASSERT_NOT_EQUAL(NULL, G_OC_MESSAGE);

    // Accessory is reset to default state, so handshake count is at 0.
    // The challenge data below consists of a MAC computed over the salt
    // '0x0102030405060708' using the 'fake origin key', with a handshake
    // count of 8.
    // {"cD": h'0102030405060708CDEE57CC88D60BE2', "cM": 0, "lS": 0}
    const struct nx_core_check_key fake_origin_key = {{0xAB}};
    uint8_t request_payload_bytes[] = {
        0xA3, 0x62, 0x63, 0x44, 0x50, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x06, 0x07, 0x08, 0xCD, 0xEE, 0x57, 0xCC, 0x88, 0xD6, 0x0B,
        0xE2, 0x62, 0x63, 0x4d, 0x00, 0x62, 0x6c, 0x53, 0x00};

    // Prepare a valid POST message with a supported challenge
    _internal_set_coap_headers(&request_packet, COAP_TYPE_NON, COAP_POST);
    coap_set_header_content_format(&request_packet, APPLICATION_VND_OCF_CBOR);
    coap_set_payload(
        &request_packet, request_payload_bytes, sizeof(request_payload_bytes));
    G_OC_MESSAGE->length =
        coap_serialize_message(&request_packet, G_OC_MESSAGE->data);

    OC_DBG("Requesting POST to '/h' URI");
    nxp_channel_symmetric_origin_key_ExpectAndReturn(fake_origin_key);
    // `request_processing` will be called to finalize the new link
    nxp_core_request_processing_Expect();
    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_CONTROLLER_ENDPOINT);
    TEST_ASSERT_TRUE(handled);

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(CREATED_2_01, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(14, response_packet.payload_len);

    // {"rD": h'D237B70650D98ED3'} (just the MAC over inverted salt)
    uint8_t expected_response_payload[] = {0xbf,
                                           0x62,
                                           0x72,
                                           0x44,
                                           0x48,
                                           0xd2,
                                           0x37,
                                           0xb7,
                                           0x6,
                                           0x50,
                                           0xd9,
                                           0x8e,
                                           0xd3,
                                           0xff};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_response_payload,
                                  response_packet.payload,
                                  response_packet.payload_len);

    PRINT("Raw CBOR Payload bytes follow:\n");
    // Print CBOR payload for demonstration
    for (uint8_t i = 0; i < response_packet.payload_len; ++i)
    {
        PRINT("%02x ", (uint8_t) * (response_packet.payload + i));
    }
    PRINT("\n");
}

void test_res_link_hs_server_post_response__supported_duplicate_mode0_command__duplicate_rejected(
    void)
{
    // assume accessory is in idle state, waiting for handshake
    _nexus_channel_res_link_hs_reset_server_state();

    // Prepare buffers
    coap_packet_t request_packet = {0};
    coap_packet_t response_packet = {0};
    uint8_t RESP_BUFFER[2048] = {0};
    // Must be deallocated at end of test.
    // Allocated on `oc_incoming_buffers` by setUp
    TEST_ASSERT_NOT_EQUAL(NULL, G_OC_MESSAGE);

    // Accessory is reset to default state, so handshake count is at 0.
    // The challenge data below consists of a MAC computed over the salt
    // '0x0102030405060708' using the 'fake origin key', with a handshake
    // count of 8.
    //  {"cD": h'0102030405060708CDEE57CC88D60BE2', "cM": 0, "lS": 0}
    const struct nx_core_check_key fake_origin_key = {{0xAB}};
    uint8_t request_payload_bytes[] = {
        0xA3, 0x62, 0x63, 0x44, 0x50, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x06, 0x07, 0x08, 0xCD, 0xEE, 0x57, 0xCC, 0x88, 0xD6, 0x0B,
        0xE2, 0x62, 0x63, 0x4d, 0x00, 0x62, 0x6c, 0x53, 0x00};

    // Prepare a valid POST message with a supported challenge
    _internal_set_coap_headers(&request_packet, COAP_TYPE_NON, COAP_POST);
    coap_set_header_content_format(&request_packet, APPLICATION_VND_OCF_CBOR);
    coap_set_payload(
        &request_packet, request_payload_bytes, sizeof(request_payload_bytes));

    G_OC_MESSAGE->length =
        coap_serialize_message(&request_packet, G_OC_MESSAGE->data);

    OC_DBG("Requesting POST to '/h' URI");

    nxp_channel_symmetric_origin_key_ExpectAndReturn(fake_origin_key);
    // `request_processing` will be called to finalize the new link
    nxp_core_request_processing_Expect();
    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_CONTROLLER_ENDPOINT);
    TEST_ASSERT_TRUE(handled);

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(CREATED_2_01, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(14, response_packet.payload_len);

    // {"rD": h'D237B70650D98ED3'} (just the MAC over inverted salt)
    uint8_t expected_response_payload[] = {0xbf,
                                           0x62,
                                           0x72,
                                           0x44,
                                           0x48,
                                           0xd2,
                                           0x37,
                                           0xb7,
                                           0x6,
                                           0x50,
                                           0xd9,
                                           0x8e,
                                           0xd3,
                                           0xff};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_response_payload,
                                  response_packet.payload,
                                  response_packet.payload_len);

    PRINT("Raw CBOR Payload bytes follow:\n");
    // Print CBOR payload for demonstration
    for (uint8_t i = 0; i < response_packet.payload_len; ++i)
    {
        PRINT("%02x ", (uint8_t) * (response_packet.payload + i));
    }
    PRINT("\n");

    // now, attempt to apply the same command again
    nxp_channel_symmetric_origin_key_ExpectAndReturn(fake_origin_key);
    // `request_processing` will be called to finalize the new link
    memset(&response_packet, 0x00, sizeof(response_packet));
    handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                               &response_packet,
                                               (void*) &RESP_BUFFER,
                                               &FAKE_CONTROLLER_ENDPOINT);
    TEST_ASSERT_TRUE(handled);

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(BAD_REQUEST_4_00, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(0, response_packet.payload_len);
}

void test_res_link_hs_server_post_response__separate_commands_window_moved__both_work(
    void)
{
    // assume accessory is in idle state, waiting for handshake
    _nexus_channel_res_link_hs_reset_server_state();

    // Prepare buffers
    coap_packet_t request_packet = {0};
    coap_packet_t response_packet = {0};
    uint8_t RESP_BUFFER[2048] = {0};
    // Must be deallocated at end of test.
    // Allocated on `oc_incoming_buffers` by setUp
    TEST_ASSERT_NOT_EQUAL(NULL, G_OC_MESSAGE);

    // Accessory is reset to default state, so handshake count is at 0.
    // The challenge data below consists of a MAC computed over the salt
    // '0x0102030405060708' using the 'fake origin key', with a handshake
    // count of 20.
    //  {"cD": h'0102030405060708C864806BCD465AFD', "cM": 0, "lS": 0}
    const struct nx_core_check_key fake_origin_key = {{0xAB}};
    uint8_t request_payload_bytes_id20[] = {
        0xA3, 0x62, 0x63, 0x44, 0x50, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x06, 0x07, 0x08, 0xC8, 0x64, 0x80, 0x6B, 0xCD, 0x46, 0x5A,
        0xFD, 0x62, 0x63, 0x4d, 0x00, 0x62, 0x6c, 0x53, 0x00};

    // Prepare a valid POST message with a supported challenge
    _internal_set_coap_headers(&request_packet, COAP_TYPE_NON, COAP_POST);
    coap_set_header_content_format(&request_packet, APPLICATION_VND_OCF_CBOR);
    coap_set_payload(&request_packet,
                     request_payload_bytes_id20,
                     sizeof(request_payload_bytes_id20));

    G_OC_MESSAGE->length =
        coap_serialize_message(&request_packet, G_OC_MESSAGE->data);

    OC_DBG("Requesting POST to '/h' URI");

    nxp_channel_symmetric_origin_key_ExpectAndReturn(fake_origin_key);
    // `request_processing` will be called to finalize the new link
    nxp_core_request_processing_Expect();

    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_CONTROLLER_ENDPOINT);
    TEST_ASSERT_TRUE(handled);

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(CREATED_2_01, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(14, response_packet.payload_len);

    // ensure the link is created (so we don't fail future attempts due to
    // a 'pending link' in this test)
    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_ACCESSORY);
    nexus_channel_core_process(0);

    // MAC over inverted salt
    uint8_t expected_response_payload[] = {0xbf,
                                           0x62,
                                           0x72,
                                           0x44,
                                           0x48,
                                           0x8d,
                                           0xc0,
                                           0xc1,
                                           0x86,
                                           0x7,
                                           0x4a,
                                           0xbb,
                                           0xe6,
                                           0xff};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_response_payload,
                                  response_packet.payload,
                                  response_packet.payload_len);

    PRINT("Raw CBOR Payload bytes follow:\n");
    // Print CBOR payload for demonstration
    for (uint8_t i = 0; i < response_packet.payload_len; ++i)
    {
        PRINT("%02x ", (uint8_t) * (response_packet.payload + i));
    }
    PRINT("\n");

    // now, attempt to apply a command with handshake ID 27
    uint8_t request_payload_bytes_id27[] = {
        0xA3, 0x62, 0x63, 0x44, 0x50, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x06, 0x07, 0x08, 0x7F, 0xF9, 0x9B, 0xF5, 0x8C, 0xB0, 0xFA,
        0x76, 0x62, 0x63, 0x4d, 0x00, 0x62, 0x6c, 0x53, 0x00};

    coap_set_payload(&request_packet,
                     request_payload_bytes_id27,
                     sizeof(request_payload_bytes_id27));

    G_OC_MESSAGE->length =
        coap_serialize_message(&request_packet, G_OC_MESSAGE->data);

    nxp_channel_symmetric_origin_key_ExpectAndReturn(fake_origin_key);
    // `request_processing` will be called to finalize the new link
    memset(&response_packet, 0x00, sizeof(response_packet));
    handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                               &response_packet,
                                               (void*) &RESP_BUFFER,
                                               &FAKE_CONTROLLER_ENDPOINT);
    TEST_ASSERT_TRUE(handled);

    // Check response code and content -- fails because we have not changed
    // the endpoint (cannot create two links to the same nexus ID)
    TEST_ASSERT_EQUAL_UINT(BAD_REQUEST_4_00, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(0, response_packet.payload_len);

    // try again from a different endpoint - should succeed
    nxp_channel_symmetric_origin_key_ExpectAndReturn(fake_origin_key);
    // `request_processing` will be called to finalize the new link
    nxp_core_request_processing_Expect();

    memset(&response_packet, 0x00, sizeof(response_packet));

    handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                               &response_packet,
                                               (void*) &RESP_BUFFER,
                                               &FAKE_CONTROLLER_ENDPOINT_B);
    TEST_ASSERT_TRUE(handled);

    TEST_ASSERT_EQUAL_UINT(CREATED_2_01, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(14, response_packet.payload_len);
}

void test_res_link_hs_challenge_mode_3_key_derivation__result_expected(void)
{
    const uint8_t salt_bytes[8] = {1, 2, 3, 5, 255, 71, 25, 10};
    const uint32_t challenge_result = 382847;

    struct nx_core_check_key link_key = _res_link_hs_generate_link_key(
        challenge_result,
        salt_bytes,
        sizeof(salt_bytes),
        &NEXUS_CHANNEL_PUBLIC_KEY_DERIVATION_KEY_1, // nexus_security.c
        &NEXUS_CHANNEL_PUBLIC_KEY_DERIVATION_KEY_2);

    OC_LOGbytes(link_key.bytes, 16);
    const struct nx_core_check_key expected = {{0x87,
                                                0x77,
                                                0xF1,
                                                0xF9,
                                                0x7C,
                                                0x86,
                                                0x40,
                                                0x8E,
                                                0x35,
                                                0x52,
                                                0xFB,
                                                0xC4,
                                                0xC9,
                                                0x03,
                                                0xF8,
                                                0x73}};

    TEST_ASSERT_EQUAL_HEX8_ARRAY(
        expected.bytes, link_key.bytes, sizeof(struct nx_core_check_key));
}

void test_res_link_hs_link_mode_3__no_free_callbacks__returns_false(void)
{
    nexus_link_hs_controller_t CHALLENGE_IN_PROGRESS = {0};
    CHALLENGE_IN_PROGRESS.state = LINK_HANDSHAKE_STATE_ACTIVE;

    // all handshake slots are active
    _nexus_channel_res_link_hs_set_client_state(&CHALLENGE_IN_PROGRESS, 0);
    _nexus_channel_res_link_hs_set_client_state(&CHALLENGE_IN_PROGRESS, 1);
    _nexus_channel_res_link_hs_set_client_state(&CHALLENGE_IN_PROGRESS, 2);
    _nexus_channel_res_link_hs_set_client_state(&CHALLENGE_IN_PROGRESS, 3);

    struct nexus_channel_om_create_link_body om_body;
    // not currently using trunc_acc_id now
    om_body.trunc_acc_id.digits_count = 0;
    om_body.trunc_acc_id.digits_int = 0;
    om_body.accessory_challenge.six_int_digits = 382847;
    oc_clock_time_IgnoreAndReturn(5); // arbitrary

    bool result = nexus_channel_res_link_hs_link_mode_3(&om_body);
    TEST_ASSERT_EQUAL(false, result);
}

// see test that follows this function
nx_channel_error
CALLBACK_test_res_link_hs_link_mode_3__send_post__sends_message_ok(
    const void* const bytes_to_send,
    uint32_t bytes_count,
    const struct nx_id* const source,
    const struct nx_id* const dest,
    bool is_multicast,
    int NumCalls)
{
    (void) source;
    (void) dest;
    (void) is_multicast;
    (void) NumCalls;
    // 4 byte CoAP header (58 02 00 7C)
    // 8 byte CoaP token (7B 00 00 00 7B 00 00 00)
    // 16 byte CoAP options
    // CBOR payload (delimited by 0xFF)
    //
    // Payload represents 16 challenge data bytes, and requested challenge
    // mode and link security mode of 0.
    //
    // BF                                     # map(*) // indefinite map
    // 62                                  # text(2)
    //  6344                             # "cD"
    // 50                                  # bytes(16)
    // 40E2010040E201008DD070D08E1836C4 #
    // "@\xE2\x01\x00@\xE2\x01\x00\x8D\xD0p\xD0\x8E\x186\xC4"
    // 62                                  # text(2)
    //  634D                             # "cM"
    // 00                                  # unsigned(0)
    // 62                                  # text(2)
    //  6C53                             # "lS"
    // 00                                  # unsigned(0)
    // FF                                  # primitive(*) // map terminator
    //
    uint8_t expected_data[59] = {
        0x58, 0x2,  0xe2, 0x41, 0x40, 0xe2, 0x1,  0x0,  0x40, 0xe2, 0x1,  0x0,
        0xb1, 0x68, 0x12, 0x27, 0x10, 0x52, 0x27, 0x10, 0xe2, 0x6,  0xe3, 0x8,
        0x0,  0x42, 0x8,  0x0,  0xff, 0xbf, 0x62, 0x63, 0x44, 0x50, 0x40, 0xe2,
        0x1,  0x0,  0x40, 0xe2, 0x1,  0x0,  0x8d, 0xd0, 0x70, 0xd0, 0x8e, 0x18,
        0x36, 0xc4, 0x62, 0x63, 0x4d, 0x0,  0x62, 0x6c, 0x53, 0x0,  0xff};
    uint8_t expected_length = 59;

    // Don't worry about the pool allocated for the message, we only care
    // about the message contents
    TEST_ASSERT_EQUAL(expected_length, bytes_count);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        expected_data, bytes_to_send, expected_length);

    TEST_ASSERT_EQUAL_MEMORY(
        &NEXUS_OC_WRAPPER_MULTICAST_NX_ID, dest, sizeof(struct nx_id));

    return NX_CHANNEL_ERROR_NONE;
}

void test_res_link_hs_link_mode_3__send_post__sends_message_ok(void)
{
    // check that `oc_do_post` is called with the right data
    struct nexus_channel_om_create_link_body om_body;
    // not currently using trunc_acc_id now
    om_body.trunc_acc_id.digits_count = 0;
    om_body.trunc_acc_id.digits_int = 0;
    om_body.accessory_challenge.six_int_digits = 382847;
    oc_clock_time_IgnoreAndReturn(5); // arbitrary

    nxp_channel_notify_event_Expect(NXP_CHANNEL_EVENT_LINK_HANDSHAKE_STARTED);
    nxp_core_request_processing_Expect(); // within origin command receipt in hs

    nxp_core_request_processing_Expect(); // within network_events
    struct nx_id fake_device_id = {0, 12345678};
    nxp_channel_get_nexus_id_ExpectAndReturn(fake_device_id);
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);

    // custom callback to more easily examine the sent message and
    // confirm the payload is valid/expected.
    // Newer name is 'func_AddCallback`, but older convention used here
    // to allow older Ceedling versions in CI to run this test.
    nxp_channel_network_send_StubWithCallback(
        CALLBACK_test_res_link_hs_link_mode_3__send_post__sends_message_ok);

    bool result = nexus_channel_res_link_hs_link_mode_3(&om_body);
    // immediately returns true, but the message is sent asynchronously
    TEST_ASSERT_EQUAL(true, result);
    // now, process so that the outbound send buffer can execute
    nexus_channel_res_link_hs_process(
        NEXUS_CHANNEL_LINK_HANDSHAKE_CONTROLLER_RETRY_SECONDS);

    // process OUTBOUND_NETWORK_EVENT in message_buffer_handler
    oc_process_run();
}

void test_res_link_hs_link_mode_3__send_post_another_post_in_progress__fails(
    void)
{
    // This test expects that the system is configured a
    // `OC_MAX_NUM_CONCURRENT_REQUESTS` which is used to derive the maximum
    // number of simultaneous client callbacks (`client_cbs` in oc_ri.c)
    // Number of `oc_init_post` here may need to change if concurrent
    // requests change (brittle test).
    oc_response_handler_t dummy_handler = {0};
    oc_clock_time_IgnoreAndReturn(5); // arbitrary

    oc_init_post(
        "dummy_uri", &FAKE_ENDPOINT, NULL, dummy_handler, LOW_QOS, NULL);
    oc_init_post(
        "dummy_uri", &FAKE_ENDPOINT, NULL, dummy_handler, LOW_QOS, NULL);

    // check that `oc_do_post` is called with the right data
    struct nexus_channel_om_create_link_body om_body;
    // not currently using trunc_acc_id now
    om_body.trunc_acc_id.digits_count = 0;
    om_body.trunc_acc_id.digits_int = 0;
    om_body.accessory_challenge.six_int_digits = 382847;

    nxp_channel_notify_event_Expect(NXP_CHANNEL_EVENT_LINK_HANDSHAKE_STARTED);
    nxp_core_request_processing_Expect();
    bool result = nexus_channel_res_link_hs_link_mode_3(&om_body);
    // will queue attempt to post , but will not have posted yet
    TEST_ASSERT_EQUAL(true, result);
    nexus_channel_res_link_hs_process(0);

    // note no expect for `nxp_channel_network_send`, will not be executed
    // process OUTBOUND_NETWORK_EVENT in message_buffer_handler
    oc_process_run();
}

// We don't expect it to be already registered, but if for a timing related
// reason we attempt to POST the handshake again, the existing callback is
// reused.
void test_res_link_hs_link_mode_3__client_cb_already_registered__attempts_reuse(
    void)
{
    oc_clock_time_IgnoreAndReturn(5); // arbitrary

    // check that `oc_do_post` is called with the right data
    struct nexus_channel_om_create_link_body om_body;
    // not currently using trunc_acc_id now
    om_body.trunc_acc_id.digits_count = 0;
    om_body.trunc_acc_id.digits_int = 0;
    om_body.accessory_challenge.six_int_digits = 382847;

    struct nx_id fake_id = {0, 1234567};

    nxp_channel_notify_event_Expect(NXP_CHANNEL_EVENT_LINK_HANDSHAKE_STARTED);
    nxp_core_request_processing_Expect();
    bool result = nexus_channel_res_link_hs_link_mode_3(&om_body);
    // will queue attempt to post , but will not have posted yet
    TEST_ASSERT_EQUAL(true, result);
    nexus_channel_res_link_hs_process(0);

    // process OUTBOUND_NETWORK_EVENT in message_buffer_handler
    oc_process_run();

    oc_client_handler_t client_handler = {0};
    oc_ri_alloc_client_cb("/h",
                          &NEXUS_OC_WRAPPER_MULTICAST_OC_ENDPOINT_T_ADDR,
                          OC_POST,
                          NULL,
                          client_handler,
                          LOW_QOS,
                          NULL);

    // trigger another execution (due to retry logic)
    // where cb will already have been allocated, still expect network to
    // send
    nxp_core_request_processing_Expect();
    nxp_channel_get_nexus_id_ExpectAndReturn(fake_id);
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);
    nexus_channel_res_link_hs_process(30);
    oc_process_run();

    // exhaust transactions, network will not send (but program will
    // still operate). Assumes transaction limit well below 100..
    for (uint8_t i = 0; i < 100; i++)
    {
        coap_new_transaction(i, &NEXUS_OC_WRAPPER_MULTICAST_OC_ENDPOINT_T_ADDR);
    }

    nexus_channel_res_link_hs_process(30);
}

void test_res_link_hs_link_mode_3__waiting_timer_not_expired__does_not_retry(
    void)
{
    nexus_link_hs_controller_t CHALLENGE_IN_PROGRESS = {0};
    CHALLENGE_IN_PROGRESS.state = LINK_HANDSHAKE_STATE_ACTIVE;

    _nexus_channel_res_link_hs_set_client_state(&CHALLENGE_IN_PROGRESS, 0);

    oc_clock_time_IgnoreAndReturn(5); // arbitrary

    nexus_link_hs_controller_t* client_hs =
        _nexus_channel_res_link_hs_get_client_state(0);

    TEST_ASSERT_EQUAL_INT(LINK_HANDSHAKE_STATE_ACTIVE, client_hs->state);
    TEST_ASSERT_EQUAL_INT(0, client_hs->last_post_seconds);

    // No retry, only one second elapsed since 'first call'
    uint32_t next_call_secs = nexus_channel_res_link_hs_process(1);
    TEST_ASSERT_EQUAL_INT(NEXUS_CHANNEL_LINK_HANDSHAKE_CONTROLLER_RETRY_SECONDS,
                          next_call_secs);

    client_hs = _nexus_channel_res_link_hs_get_client_state(0);
    TEST_ASSERT_EQUAL_INT(LINK_HANDSHAKE_STATE_ACTIVE, client_hs->state);
    TEST_ASSERT_EQUAL_INT(0, client_hs->last_post_seconds);
}

void test_res_link_hs_link_mode_3__retries_post__times_out_eventually(void)
{
    nexus_link_hs_controller_t CHALLENGE_IN_PROGRESS = {0};
    CHALLENGE_IN_PROGRESS.state = LINK_HANDSHAKE_STATE_ACTIVE;

    _nexus_channel_res_link_hs_set_client_state(&CHALLENGE_IN_PROGRESS, 0);

    oc_clock_time_IgnoreAndReturn(5); // arbitrary

    // should not time out, should retry. Will call request processing
    // as well
    nxp_core_request_processing_Expect();

    struct nx_id fake_device_id = {0, 12345678};
    // expect to send another POST after retrying
    nxp_channel_get_nexus_id_ExpectAndReturn(fake_device_id);
    nxp_channel_network_send_IgnoreAndReturn(NX_CHANNEL_ERROR_NONE);

    uint32_t next_call_secs = nexus_channel_res_link_hs_process(
        NEXUS_CHANNEL_LINK_HANDSHAKE_CONTROLLER_TIMEOUT_SECONDS / 2);
    TEST_ASSERT_EQUAL_INT(NEXUS_CHANNEL_LINK_HANDSHAKE_CONTROLLER_RETRY_SECONDS,
                          next_call_secs);

    nexus_link_hs_controller_t* client_hs =
        _nexus_channel_res_link_hs_get_client_state(0);
    TEST_ASSERT_EQUAL_INT(LINK_HANDSHAKE_STATE_ACTIVE, client_hs->state);

    // process OUTBOUND_NETWORK_EVENT in message_buffer_handler
    oc_process_run();
    // free the client callback so we can reallocate it next run
    // assumes that the handshake POST is sent to the multicast endpoint
    oc_ri_free_client_cbs_by_endpoint(&MCAST_ENDPOINT);

    // don't expect any more OC calls
    next_call_secs = nexus_channel_res_link_hs_process(
        NEXUS_CHANNEL_LINK_HANDSHAKE_CONTROLLER_TIMEOUT_SECONDS);
    TEST_ASSERT_EQUAL_INT(NEXUS_CHANNEL_LINK_HANDSHAKE_CONTROLLER_RETRY_SECONDS,
                          next_call_secs);

    client_hs = _nexus_channel_res_link_hs_get_client_state(0);
    TEST_ASSERT_EQUAL_INT(LINK_HANDSHAKE_STATE_IDLE, client_hs->state);
}

void test_res_link_hs_link_mode_3__accepted_post_response__creates_link(void)
{
    OC_DBG("Testing simulated response to handshake challenge");
    // set up the client to expect a response based on the data sent here
    // Set internal resource state
    nexus_link_hs_controller_t CHALLENGE_IN_PROGRESS = {0};
    CHALLENGE_IN_PROGRESS.state = LINK_HANDSHAKE_STATE_ACTIVE;
    // arbitrary link key and salt
    memset(&CHALLENGE_IN_PROGRESS.link_key.bytes[0],
           0x1F,
           sizeof(CHALLENGE_IN_PROGRESS.link_key.bytes));
    memset(
        &CHALLENGE_IN_PROGRESS.salt, 0xAB, sizeof(CHALLENGE_IN_PROGRESS.salt));

    _nexus_channel_res_link_hs_set_client_state(&CHALLENGE_IN_PROGRESS, 0);

    oc_client_handler_t client_handler;
    client_handler.response = nexus_channel_res_link_hs_client_post;
    // register a fake client callback, similar to what happens in
    // `oc_init_post`
    oc_clock_time_ExpectAndReturn(5); // arbitrary timestamp for callback

    // the user data must be set as well so that the POST can can
    // complete the handshake - otherwise, it will attempt to deference
    // NULL user data.
    G_OC_CLIENT_CB =
        oc_ri_alloc_client_cb("/h",
                              &MCAST_ENDPOINT,
                              OC_POST,
                              NULL,
                              client_handler,
                              LOW_QOS,
                              _nexus_channel_res_link_hs_get_client_state(0));

    // The response will be sent to the callback found
    // via `oc_ri_find_client_cb_by_token`, so we need to set our response
    // message to the same token that the 'outbound' request message had.
    coap_packet_t resp_packet = {0};
    // Most of the following is directly from `prepare_coap_message` in
    // `oc_client_api.c
    // type code of response is '2.05/OK'
    coap_udp_init_message(
        &resp_packet, COAP_TYPE_NON, CREATED_2_01, G_OC_CLIENT_CB->mid);
    coap_set_header_accept(&resp_packet, APPLICATION_VND_OCF_CBOR);
    coap_set_token(
        &resp_packet, G_OC_CLIENT_CB->token, G_OC_CLIENT_CB->token_len);
    coap_set_header_uri_path(&resp_packet,
                             oc_string(G_OC_CLIENT_CB->uri),
                             oc_string_len(G_OC_CLIENT_CB->uri));

    TEST_ASSERT_EQUAL(G_OC_CLIENT_CB->mid, resp_packet.mid);

    // add a payload with the 'expected' response data, for now, its just
    // the presence of the 'resp data' field (contents not checked).
    // {0xC7, 0x9B, 0x59, 0xC8, 0x23, 0x58, 0x35, 0x9E} is MAC computed over
    // the inverted salt and link key selected in this test.
    uint8_t resp_data_cbor[14] = {0xBF,
                                  0x62,
                                  0x72,
                                  0x44,
                                  0x48,
                                  0xC7,
                                  0x9B,
                                  0x59,
                                  0xC8,
                                  0x23,
                                  0x58,
                                  0x35,
                                  0x9E,
                                  0xFF};

    coap_set_payload(&resp_packet, resp_data_cbor, 14);

    // Create a serialized CoAP message so we can simulate receiving it
    // allocate because `oc_network_event` will attempt to unref it later
    TEST_ASSERT_NOT_EQUAL(NULL, G_OC_MESSAGE);
    G_OC_MESSAGE->length =
        coap_serialize_message(&resp_packet, G_OC_MESSAGE->data);
    oc_endpoint_copy(&G_OC_MESSAGE->endpoint, &FAKE_ACCESSORY_ENDPOINT);

    // ensure that the handshake is in progress before response
    // is received
    nexus_link_hs_controller_t* client_hs =
        _nexus_channel_res_link_hs_get_client_state(0);
    TEST_ASSERT_EQUAL_INT(LINK_HANDSHAKE_STATE_ACTIVE, client_hs->state);

    // will call `oc_recv_message` -> `oc_process_post` to network event handler
    // -> `oc_process_post` to coap engine (INBOUND_RI_EVENT)
    // -> `coap_receive(data)`
    // message is then parsed, then the message code is detected. Since its
    // not a request, it will jump to line ~580 in `engine.c` which then
    // finds the client callback by message token,
    // oc_network_event will unref the message, no need to do so here
    oc_network_event(G_OC_MESSAGE);

    // One call from `nexus_channel_link_manager_create_link`
    nxp_core_request_processing_Expect();
    nxp_core_request_processing_Expect();

    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_CONTROLLER);

    nexus_channel_core_process(0);

    // should have been completed
    client_hs = _nexus_channel_res_link_hs_get_client_state(0);
    TEST_ASSERT_EQUAL_INT(LINK_HANDSHAKE_STATE_IDLE, client_hs->state);
    // 1 link now exists
    TEST_ASSERT_EQUAL_INT(1, nx_channel_link_count());
}

void test_res_link_hs_link_mode_3__post_response_invalid_mac__no_link_created(
    void)
{
    OC_DBG("Testing simulated response to handshake challenge");
    // set up the client to expect a response based on the data sent here
    // Set internal resource state
    nexus_link_hs_controller_t CHALLENGE_IN_PROGRESS = {0};
    CHALLENGE_IN_PROGRESS.state = LINK_HANDSHAKE_STATE_ACTIVE;
    // arbitrary link key and salt
    memset(&CHALLENGE_IN_PROGRESS.link_key.bytes[0],
           0x1F,
           sizeof(CHALLENGE_IN_PROGRESS.link_key.bytes));
    memset(
        &CHALLENGE_IN_PROGRESS.salt, 0xAB, sizeof(CHALLENGE_IN_PROGRESS.salt));

    _nexus_channel_res_link_hs_set_client_state(&CHALLENGE_IN_PROGRESS, 0);

    oc_client_handler_t client_handler;
    client_handler.response = nexus_channel_res_link_hs_client_post;
    // register a fake client callback, similar to what happens in
    // `oc_init_post`
    oc_clock_time_ExpectAndReturn(5); // arbitrary timestamp for callback

    // the user data must be set as well so that the POST can can
    // complete the handshake - otherwise, it will attempt to deference
    // NULL user data.
    G_OC_CLIENT_CB =
        oc_ri_alloc_client_cb("/h",
                              &MCAST_ENDPOINT,
                              OC_POST,
                              NULL,
                              client_handler,
                              LOW_QOS,
                              _nexus_channel_res_link_hs_get_client_state(0));

    // The response will be sent to the callback found
    // via `oc_ri_find_client_cb_by_token`, so we need to set our response
    // message to the same token that the 'outbound' request message had.
    coap_packet_t resp_packet = {0};
    // Most of the following is directly from `prepare_coap_message` in
    // `oc_client_api.c
    // type code of response is '2.05/OK'
    coap_udp_init_message(
        &resp_packet, COAP_TYPE_NON, CREATED_2_01, G_OC_CLIENT_CB->mid);
    coap_set_header_accept(&resp_packet, APPLICATION_VND_OCF_CBOR);
    coap_set_token(
        &resp_packet, G_OC_CLIENT_CB->token, G_OC_CLIENT_CB->token_len);
    coap_set_header_uri_path(&resp_packet,
                             oc_string(G_OC_CLIENT_CB->uri),
                             oc_string_len(G_OC_CLIENT_CB->uri));

    TEST_ASSERT_EQUAL(G_OC_CLIENT_CB->mid, resp_packet.mid);

    // add a payload with the 'expected' response data, for now, its just
    // the presence of the 'resp data' field (contents not checked).
    // {0xC7, 0x9B, 0x59, 0xC8, 0x23, 0x58, 0x35, 0x9E} is MAC computed over
    // the inverted salt and link key selected in this test.
    // (9E changed to 9D, invalid)
    uint8_t resp_data_cbor[14] = {0xBF,
                                  0x62,
                                  0x72,
                                  0x44,
                                  0x48,
                                  0xC7,
                                  0x9B,
                                  0x59,
                                  0xC8,
                                  0x23,
                                  0x58,
                                  0x35,
                                  0x9D,
                                  0xFF};

    coap_set_payload(&resp_packet, resp_data_cbor, 14);

    // Create a serialized CoAP message so we can simulate receiving it
    // allocate because `oc_network_event` will attempt to unref it later
    TEST_ASSERT_NOT_EQUAL(NULL, G_OC_MESSAGE);
    G_OC_MESSAGE->length =
        coap_serialize_message(&resp_packet, G_OC_MESSAGE->data);
    oc_endpoint_copy(&G_OC_MESSAGE->endpoint, &FAKE_ACCESSORY_ENDPOINT);

    // ensure that the handshake is in progress before response
    // is received
    nexus_link_hs_controller_t* client_hs =
        _nexus_channel_res_link_hs_get_client_state(0);
    TEST_ASSERT_EQUAL_INT(LINK_HANDSHAKE_STATE_ACTIVE, client_hs->state);

    // will call `oc_recv_message` -> `oc_process_post` to network event handler
    // -> `oc_process_post` to coap engine (INBOUND_RI_EVENT)
    // -> `coap_receive(data)`
    // message is then parsed, then the message code is detected. Since its
    // not a request, it will jump to line ~580 in `engine.c` which then
    // finds the client callback by message token,
    // oc_network_event will unref the message, no need to do so here
    oc_network_event(G_OC_MESSAGE);

    // `nxp_core_request_processing` should result in a call to core process
    nexus_channel_core_process(0);

    // should not have been completed
    client_hs = _nexus_channel_res_link_hs_get_client_state(0);
    TEST_ASSERT_EQUAL_INT(LINK_HANDSHAKE_STATE_ACTIVE, client_hs->state);
    // no links exist
    TEST_ASSERT_EQUAL_INT(0, nx_channel_link_count());
}

// testing handshake challenge mode 0 controller->accessory interpretation.
// 'challenge int' values are computed for an accessory with key
// `b'\xc4\xb8@H\xcf\x04$\xa2]\xc5\xe9\xd3\xf0g@6` for consistency with
// the backend.
void test_res_link_hs_server_post_finalize_state__move_window_right__preserves_ids(
    void)
{
    // challenge data received from controller consists of a MAC which
    // was computed over the salt (from the controller) and challenge int
    // (from the backend)
    struct nexus_window window;
    memset(&window, 0x00, sizeof(struct nexus_window));
    _nexus_channel_res_link_hs_get_current_window(&window);
    const struct nx_core_check_key ACCESSORY_KEY = {{0xC4,
                                                     0xB8,
                                                     0x40,
                                                     0x48,
                                                     0xCF,
                                                     0x04,
                                                     0x24,
                                                     0xA2,
                                                     0x5D,
                                                     0xC5,
                                                     0xE9,
                                                     0xD3,
                                                     0xF0,
                                                     0x67,
                                                     0x40,
                                                     0x36}};

    struct test_scenario
    {
        uint8_t transmitted_challenge[16]; // handshake mode0 specific.
        uint32_t expected_challenge_int_digits;
        uint32_t expected_handshake_index;
    };
    struct test_scenario scenarios[] = {
        {{0}, 387852, 0},
        {{0}, 321175, 8},
        {{0}, 45133, 9},
        {{0}, 752435, 15},
        {{0}, 960827, 23},
        {{0}, 645026, 31},
        {{0}, 483412, 32},
    };

    // first pass, all should be accepted
    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        const uint8_t salt[8] = {
            0x01, 0x02, 0x03, 0x04, 0xFF, 0xA0, 0x0B, 0xEE};
        // call controller function to generate MAC from challenge int and salt

        struct test_scenario scenario = scenarios[i];
        struct nx_core_check_key expected_link_key =
            _res_link_hs_generate_link_key(
                scenario.expected_challenge_int_digits,
                salt,
                8,
                &NEXUS_CHANNEL_PUBLIC_KEY_DERIVATION_KEY_1,
                &NEXUS_CHANNEL_PUBLIC_KEY_DERIVATION_KEY_2);
        const struct nexus_check_value controller_mac =
            nexus_check_compute(&expected_link_key, &salt, sizeof(salt));
        // copy into transmitted challenge
        memcpy(scenario.transmitted_challenge, salt, sizeof(salt));
        memcpy(&scenario.transmitted_challenge[8],
               &controller_mac,
               sizeof(controller_mac));

        uint32_t matched_handshake_index;
        // receive transmitted challenge on accessory side. Should be validated
        // immediately using the expected digits.
        nxp_channel_symmetric_origin_key_ExpectAndReturn(ACCESSORY_KEY);
        struct nx_core_check_key derived_link_key;
        bool challenge_validated =
            _nexus_channel_res_link_hs_server_validate_challenge(
                &scenario.transmitted_challenge[0],
                &controller_mac,
                &window,
                &matched_handshake_index,
                &derived_link_key);
        TEST_ASSERT_TRUE(challenge_validated);
        TEST_ASSERT_EQUAL_UINT(scenario.expected_handshake_index,
                               matched_handshake_index);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_link_key.bytes,
                                      derived_link_key.bytes,
                                      sizeof(expected_link_key.bytes));

        // finalize success state to update NV
        _nexus_channel_res_link_hs_server_post_finalize_success_state(
            matched_handshake_index, &window, &derived_link_key);
    }

    // second pass, all should be rejected
    for (uint8_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        const uint8_t salt[8] = {
            0x01, 0x02, 0x03, 0x04, 0xFF, 0xA0, 0x0B, 0xEE};
        // call controller function to generate MAC from challenge int and salt

        struct test_scenario scenario = scenarios[i];
        struct nx_core_check_key expected_link_key =
            _res_link_hs_generate_link_key(
                scenario.expected_challenge_int_digits,
                salt,
                8,
                &NEXUS_CHANNEL_PUBLIC_KEY_DERIVATION_KEY_1,
                &NEXUS_CHANNEL_PUBLIC_KEY_DERIVATION_KEY_2);
        const struct nexus_check_value controller_mac =
            nexus_check_compute(&expected_link_key, &salt, sizeof(salt));
        // copy into transmitted challenge
        memcpy(scenario.transmitted_challenge, salt, sizeof(salt));
        memcpy(&scenario.transmitted_challenge[8],
               &controller_mac,
               sizeof(controller_mac));

        uint32_t matched_handshake_index;

        // receive transmitted challenge on accessory side. Should be validated
        // immediately using the expected digits.
        nxp_channel_symmetric_origin_key_ExpectAndReturn(ACCESSORY_KEY);
        struct nx_core_check_key derived_link_key;
        bool challenge_validated =
            _nexus_channel_res_link_hs_server_validate_challenge(
                &scenario.transmitted_challenge[0],
                &controller_mac,
                &window,
                &matched_handshake_index,
                &derived_link_key);
        TEST_ASSERT_FALSE(challenge_validated);

        // finalize success state to update NV
        _nexus_channel_res_link_hs_server_post_finalize_success_state(
            matched_handshake_index, &window, &derived_link_key);
    }
}
/*

void test_res_link_hs_client_post_cb__null_data__returns_early(void)
{
    // just ensure it does *not* segfault by calling the function
    oc_client_response_t* null_post_resp = 0x0;
    nexus_channel_res_link_hs_client_post(null_post_resp);

    // test with null user data as well - initialize to null
    oc_client_response_t dummy_post_resp = {0};
    dummy_post_resp.user_data = (void*) 0x0;
    nexus_channel_res_link_hs_client_post(&dummy_post_resp);
}

void test_res_link_hs_client_get_cb__dummy__todo(void)
{
    oc_client_response_t dummy_get_resp;
    dummy_get_resp.code = OC_STATUS_OK;
    nexus_channel_res_link_hs_client_get(&dummy_get_resp);
    dummy_get_resp.code = OC_STATUS_FORBIDDEN;
    nexus_channel_res_link_hs_client_get(&dummy_get_resp);

    TEST_ASSERT_TRUE(1);
}
*/
