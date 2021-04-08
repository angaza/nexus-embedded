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
#include "src/nexus_common_internal.h"
#include "src/nexus_keycode_core.h"
#include "src/nexus_keycode_mas.h"
#include "src/nexus_keycode_pro.h"
#include "src/nexus_keycode_pro_extended.h"
#include "src/nexus_nv.h"
#include "src/nexus_oc_wrapper.h"
#include "src/nexus_security.h"
#include "src/nexus_util.h"
#include "unity.h"
#include "utils/crc_ccitt.h"
#include "utils/siphash_24.h"

// Other support libraries
#include <mock_nxp_channel.h>
#include <mock_nxp_common.h>
#include <mock_nxp_keycode.h>
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
oc_endpoint_t FAKE_ENDPOINT_A = {
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

oc_endpoint_t FAKE_ENDPOINT_B = {
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

// Global message that can be allocated and deallocated at start and end
// of tests regardless of failures
static oc_message_t* G_OC_MESSAGE = 0;
static oc_rep_t* G_OC_REP = 0;
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
    nxp_common_nv_read_IgnoreAndReturn(true);
    nxp_common_nv_write_IgnoreAndReturn(true);
    nxp_channel_random_value_IgnoreAndReturn(123456);
    // register platform and device
    nexus_channel_core_init();

    // In tests, `nexus_channel_core_init` does not initialize channel
    // submodules,
    // so we can enable just this submodule manually
    nexus_channel_res_link_hs_init();
    // also need link manager to be initialized, since handshakes create
    // links
    nexus_channel_link_manager_init();

    // initialize in 'disabled' state
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    nexus_channel_res_payg_credit_init();

    // confirm that the initialized resource is valid/present
    // assumes device is at index '0'
    oc_resource_t* resource =
        oc_ri_get_app_resource_by_uri("c", 1, NEXUS_CHANNEL_NEXUS_DEVICE_ID);
    TEST_ASSERT_EQUAL_STRING_LEN("/c", resource->uri.ptr, 2);
    TEST_ASSERT_EQUAL_STRING_LEN("angaza.com.nexus.payg_credit",
                                 resource->types.ptr,
                                 strlen("angaza.com.nexus.payg_credit"));

    // will prepare CoAP engine to send/receive messages
    coap_init_engine();

    // must be deallocated at end of test
    G_OC_MESSAGE = oc_allocate_message();
    G_OC_REP = 0;
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
    if (G_OC_REP != 0)
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
    coap_set_header_uri_path(request_packet, "/c", strlen("/c"));
}

void test_payg_credit_init__is_an_accessory__initializes_with_no_credit(void)
{
    // we perform a custom setup for this function, as we want to simulate
    // a link being present before initializing the PAYG credit module
    nexus_channel_core_shutdown();
    oc_nexus_testing_reinit_mmem_lists();
    oc_message_unref(G_OC_MESSAGE);

    nexus_channel_core_init();
    nexus_channel_res_link_hs_init();
    nexus_channel_link_manager_init();

    struct nx_id linked_cont_id = {5921, 123458};

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_create_link(
        &linked_cont_id,
        CHANNEL_LINK_OPERATING_MODE_ACCESSORY,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_ACCESSORY);
    nexus_channel_link_manager_process(0);

    // re-initialize payg credit, should detect that it is an accessory, and
    // enter dependent mode. Should retrieve the remaining credit from the
    // product
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_ENABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(54021);
    nexus_channel_res_payg_credit_init();

    TEST_ASSERT_EQUAL(54021, _nexus_channel_payg_credit_remaining_credit());
    enum nexus_channel_payg_credit_operating_mode mode =
        _nexus_channel_res_payg_credit_get_credit_operating_mode();
    TEST_ASSERT_EQUAL(NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_FOLLOWING, mode);
}

void test_payg_credit_init__is_an_unlinked_unlocked_accessory__initializes_unlocked(
    void)
{
    // we perform a custom setup for this function, as we want to simulate
    // a link being present before initializing the PAYG credit module
    nexus_channel_core_shutdown();
    oc_nexus_testing_reinit_mmem_lists();
    oc_message_unref(G_OC_MESSAGE);

    nexus_channel_core_init();
    nexus_channel_res_link_hs_init();
    nexus_channel_link_manager_init();

    // re-initialize payg credit, should detect that it is an accessory, and
    // has no link but is unlocked.
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_UNLOCKED);
    nexus_channel_res_payg_credit_init();

    TEST_ASSERT_EQUAL(UINT32_MAX,
                      _nexus_channel_payg_credit_remaining_credit());
    enum nexus_channel_payg_credit_operating_mode mode =
        _nexus_channel_res_payg_credit_get_credit_operating_mode();
    TEST_ASSERT_EQUAL(NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_DISCONNECTED,
                      mode);
}

void test_payg_credit_get_response__default_with_baseline__cbor_data_model_correct(
    void)
{
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

    OC_DBG("Requesting GET to '/c' URI with baseline");

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(86437);
    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_ENDPOINT_A);
    TEST_ASSERT_TRUE(handled);

    PRINT("Raw CBOR Payload bytes follow (1):\n");
    /* {"rt": ["angaza.com.nexus.payg_credit"], "if": ["oic.if.rw",
     * "oic.if.baseline"], "mo": 0, "re": 86437, "sM": [0, 1, 2, 3], "un":
     * "seconds"}
     */
    uint8_t expected_payload_bytes[] = {
        0xbf, 0x62, 0x72, 0x74, 0x9f, 0x78, 0x1c, 0x61, 0x6e, 0x67, 0x61, 0x7a,
        0x61, 0x2e, 0x63, 0x6f, 0x6d, 0x2e, 0x6e, 0x65, 0x78, 0x75, 0x73, 0x2e,
        0x70, 0x61, 0x79, 0x67, 0x5f, 0x63, 0x72, 0x65, 0x64, 0x69, 0x74, 0xff,
        0x62, 0x69, 0x66, 0x9f, 0x69, 0x6f, 0x69, 0x63, 0x2e, 0x69, 0x66, 0x2e,
        0x72, 0x77, 0x6f, 0x6f, 0x69, 0x63, 0x2e, 0x69, 0x66, 0x2e, 0x62, 0x61,
        0x73, 0x65, 0x6c, 0x69, 0x6e, 0x65, 0xff, 0x62, 0x6d, 0x6f, 0x0,  0x62,
        0x72, 0x65, 0x1a, 0x0,  0x1,  0x51, 0xa5, 0x62, 0x73, 0x4d, 0x9f, 0x0,
        0x1,  0x2,  0x3,  0xff, 0x62, 0x75, 0x6e, 0x67, 0x73, 0x65, 0x63, 0x6f,
        0x6e, 0x64, 0x73, 0xff};

    for (uint8_t i = 0; i < response_packet.payload_len; ++i)
    {
        PRINT("%02x ", (uint8_t) * (response_packet.payload + i));
        TEST_ASSERT_EQUAL_UINT(expected_payload_bytes[i],
                               (uint8_t) * (response_packet.payload + i));
    }
    PRINT("\n");

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(CONTENT_2_05, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(100, response_packet.payload_len);

    _initialize_oc_rep_pool();
    // ensure that the message is parseable
    int success = oc_parse_rep(response_packet.payload, // payload,
                               response_packet.payload_len,
                               &G_OC_REP);
    TEST_ASSERT_EQUAL(0, success);
}

void test_payg_credit_server_get_response__no_baseline_accessory_mode__shows_dependent_mode(
    void)
{
    // set up a link to another device which is controlling this one
    struct nx_id linked_cont_id = {5921, 123458};

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_create_link(
        &linked_cont_id,
        CHANNEL_LINK_OPERATING_MODE_ACCESSORY,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_ACCESSORY);
    nexus_channel_link_manager_process(0);

    // Prepare buffers
    coap_packet_t request_packet = {0};
    coap_packet_t response_packet = {0};
    uint8_t RESP_BUFFER[2048] = {0};
    // Must be deallocated at end of test.
    // Allocate on `oc_incoming_buffers`
    TEST_ASSERT_NOT_EQUAL(NULL, G_OC_MESSAGE);

    // Prepare a GET message with baseline interface
    _internal_set_coap_headers(&request_packet, COAP_TYPE_NON, COAP_GET);

    G_OC_MESSAGE->length =
        coap_serialize_message(&request_packet, G_OC_MESSAGE->data);

    OC_DBG("Requesting GET to '/c' URI with no baseline interface");

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_ENABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(1209600);
    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_ENDPOINT_A);
    TEST_ASSERT_TRUE(handled);

    PRINT("Raw CBOR Payload bytes follow (1):\n");
    // expect 'dependent' mode, as this device is linked as an accessory to
    // another controller device
    // {"mo": 2, "re": 1209600, "sM": [0, 1, 2, 3], "un": "seconds"}
    uint8_t expected_payload_bytes[] = {
        0xbf, 0x62, 0x6d, 0x6f, 0x2,  0x62, 0x72, 0x65, 0x1a, 0x0,  0x12, 0x75,
        0x0,  0x62, 0x73, 0x4d, 0x9f, 0x0,  0x1,  0x2,  0x3,  0xff, 0x62, 0x75,
        0x6e, 0x67, 0x73, 0x65, 0x63, 0x6f, 0x6e, 0x64, 0x73, 0xff};

    for (uint8_t i = 0; i < response_packet.payload_len; ++i)
    {
        PRINT("%02x ", (uint8_t) * (response_packet.payload + i));
        TEST_ASSERT_EQUAL_UINT(expected_payload_bytes[i],
                               (uint8_t) * (response_packet.payload + i));
    }
    PRINT("\n");

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(CONTENT_2_05, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(34, response_packet.payload_len);

    _initialize_oc_rep_pool();
    // ensure that the message is parseable
    int success = oc_parse_rep(response_packet.payload, // payload,
                               response_packet.payload_len,
                               &G_OC_REP);
    TEST_ASSERT_EQUAL(0, success);
}

void test_payg_credit_server_post_from_linked_controller__re_parameter_missing__rejected(
    void)
{
    // set up a link to another device which is controlling this one
    struct nx_id linked_cont_id = {5921, 123458};

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_create_link(
        &linked_cont_id,
        CHANNEL_LINK_OPERATING_MODE_ACCESSORY,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_ACCESSORY);
    nexus_channel_link_manager_process(0);

    // Prepare buffers
    coap_packet_t request_packet = {0};
    coap_packet_t response_packet = {0};
    uint8_t RESP_BUFFER[2048] = {0};
    // Must be deallocated at end of test.
    // Allocate on `oc_incoming_buffers`
    TEST_ASSERT_NOT_EQUAL(NULL, G_OC_MESSAGE);

    // {"credit": 1234} // not the expected parameter, "re(maining)"
    uint8_t request_payload_bytes[] = {
        0xA1, 0x66, 0x63, 0x72, 0x65, 0x64, 0x69, 0x74, 0x19, 0x04, 0xD2};
    _internal_set_coap_headers(&request_packet, COAP_TYPE_NON, COAP_POST);
    coap_set_header_content_format(&request_packet, APPLICATION_VND_OCF_CBOR);

    coap_set_payload(
        &request_packet, request_payload_bytes, sizeof(request_payload_bytes));

    G_OC_MESSAGE->length =
        coap_serialize_message(&request_packet, G_OC_MESSAGE->data);

    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_ENDPOINT_A);
    TEST_ASSERT_TRUE(handled);
    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(BAD_REQUEST_4_00, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(0, response_packet.payload_len);

    // no payload to parse.
}

void test_payg_credit_server_post_from_linked_controller__credit_not_integer__rejected(
    void)
{
    // set up a link to another device which is controlling this one
    struct nx_id linked_cont_id = {5921, 123458};

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_create_link(
        &linked_cont_id,
        CHANNEL_LINK_OPERATING_MODE_ACCESSORY,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_ACCESSORY);
    nexus_channel_link_manager_process(0);

    // Prepare buffers
    coap_packet_t request_packet = {0};
    coap_packet_t response_packet = {0};
    uint8_t RESP_BUFFER[2048] = {0};
    // Must be deallocated at end of test.
    // Allocate on `oc_incoming_buffers`
    TEST_ASSERT_NOT_EQUAL(NULL, G_OC_MESSAGE);

    // {"re": "1234"}  // not integer
    uint8_t request_payload_bytes[] = {
        0xA1, 0x62, 0x72, 0x65, 0x64, 0x31, 0x32, 0x33, 0x34};
    _internal_set_coap_headers(&request_packet, COAP_TYPE_NON, COAP_POST);
    coap_set_header_content_format(&request_packet, APPLICATION_VND_OCF_CBOR);

    coap_set_payload(
        &request_packet, request_payload_bytes, sizeof(request_payload_bytes));

    G_OC_MESSAGE->length =
        coap_serialize_message(&request_packet, G_OC_MESSAGE->data);

    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_ENDPOINT_A);
    TEST_ASSERT_TRUE(handled);
    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(BAD_REQUEST_4_00, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(0, response_packet.payload_len);

    // no payload to parse.
}

void test_payg_credit_server_post_from_linked_controller__credit_out_of_range__rejected(
    void)
{
    // set up a link to another device which is controlling this one
    struct nx_id linked_cont_id = {5921, 123458};

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_create_link(
        &linked_cont_id,
        CHANNEL_LINK_OPERATING_MODE_ACCESSORY,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_ACCESSORY);
    nexus_channel_link_manager_process(0);

    // Prepare buffers
    coap_packet_t request_packet = {0};
    coap_packet_t response_packet = {0};
    uint8_t RESP_BUFFER[2048] = {0};
    // Must be deallocated at end of test.
    // Allocate on `oc_incoming_buffers`
    TEST_ASSERT_NOT_EQUAL(NULL, G_OC_MESSAGE);

    // {"re": 8589934590}  // too big for uint32_t
    uint8_t request_payload_bytes[] = {0xA1,
                                       0x62,
                                       0x72,
                                       0x65,
                                       0x1B,
                                       0x00,
                                       0x00,
                                       0x00,
                                       0x01,
                                       0xFF,
                                       0xFF,
                                       0xFF,
                                       0xFE};
    _internal_set_coap_headers(&request_packet, COAP_TYPE_NON, COAP_POST);
    coap_set_header_content_format(&request_packet, APPLICATION_VND_OCF_CBOR);

    coap_set_payload(
        &request_packet, request_payload_bytes, sizeof(request_payload_bytes));

    G_OC_MESSAGE->length =
        coap_serialize_message(&request_packet, G_OC_MESSAGE->data);

    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_ENDPOINT_A);
    TEST_ASSERT_TRUE(handled);
    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(BAD_REQUEST_4_00, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(0, response_packet.payload_len);

    // no payload to parse.
}

void test_payg_credit_server_post_from_linked_controller__accepted_credit_updated(
    void)
{
    // set up a link to another device which is controlling this one
    struct nx_id linked_cont_id = {5921, 123458};

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_create_link(
        &linked_cont_id,
        CHANNEL_LINK_OPERATING_MODE_ACCESSORY,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_ACCESSORY);
    nexus_channel_link_manager_process(0);

    // Prepare buffers
    coap_packet_t request_packet = {0};
    coap_packet_t response_packet = {0};
    uint8_t RESP_BUFFER[2048] = {0};
    // Must be deallocated at end of test.
    // Allocate on `oc_incoming_buffers`
    TEST_ASSERT_NOT_EQUAL(NULL, G_OC_MESSAGE);

    // {"re": 12345678}
    uint8_t request_payload_bytes[] = {
        0xA1, 0x62, 0x72, 0x65, 0x1A, 0x00, 0xBC, 0x61, 0x4E};
    _internal_set_coap_headers(&request_packet, COAP_TYPE_NON, COAP_POST);
    coap_set_header_content_format(&request_packet, APPLICATION_VND_OCF_CBOR);

    coap_set_payload(
        &request_packet, request_payload_bytes, sizeof(request_payload_bytes));

    G_OC_MESSAGE->length =
        coap_serialize_message(&request_packet, G_OC_MESSAGE->data);

    nxp_channel_payg_credit_set_ExpectAndReturn(12345678,
                                                NX_CHANNEL_ERROR_NONE);
    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_ENDPOINT_A);
    TEST_ASSERT_TRUE(handled);
    // POST response only includes remaining credit value, and units
    // {"re": 12345678, "un": "seconds"}
    uint8_t expected_payload_bytes[] = {
        0xbf, 0x62, 0x72, 0x65, 0x1a, 0x0,  0xbc, 0x61, 0x4e, 0x62, 0x75,
        0x6e, 0x67, 0x73, 0x65, 0x63, 0x6f, 0x6e, 0x64, 0x73, 0xff};
    for (uint8_t i = 0; i < response_packet.payload_len; ++i)
    {
        PRINT("%02x ", (uint8_t) * (response_packet.payload + i));
        TEST_ASSERT_EQUAL_UINT(expected_payload_bytes[i],
                               (uint8_t) * (response_packet.payload + i));
    }
    PRINT("\n");

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(CHANGED_2_04, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(21, response_packet.payload_len);

    _initialize_oc_rep_pool();
    // ensure that the message is parseable
    int success = oc_parse_rep(response_packet.payload, // payload,
                               response_packet.payload_len,
                               &G_OC_REP);
    TEST_ASSERT_EQUAL(0, success);
}

void test_payg_credit_server_post_from_linked_controller__unlock_credit__device_unlocked(
    void)
{
    // set up a link to another device which is controlling this one
    struct nx_id linked_cont_id = {5921, 123458};

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_create_link(
        &linked_cont_id,
        CHANNEL_LINK_OPERATING_MODE_ACCESSORY,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_ACCESSORY);
    nexus_channel_link_manager_process(0);

    // Prepare buffers
    coap_packet_t request_packet = {0};
    coap_packet_t response_packet = {0};
    uint8_t RESP_BUFFER[2048] = {0};
    // Must be deallocated at end of test.
    // Allocate on `oc_incoming_buffers`
    TEST_ASSERT_NOT_EQUAL(NULL, G_OC_MESSAGE);

    // {"re": 4294967295} (uint32_max)
    uint8_t request_payload_bytes[] = {
        0xA1, 0x62, 0x72, 0x65, 0x1A, 0xFF, 0xFF, 0xFF, 0xFF};
    _internal_set_coap_headers(&request_packet, COAP_TYPE_NON, COAP_POST);
    coap_set_header_content_format(&request_packet, APPLICATION_VND_OCF_CBOR);

    coap_set_payload(
        &request_packet, request_payload_bytes, sizeof(request_payload_bytes));

    G_OC_MESSAGE->length =
        coap_serialize_message(&request_packet, G_OC_MESSAGE->data);

    nxp_channel_payg_credit_unlock_ExpectAndReturn(NX_CHANNEL_ERROR_NONE);
    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_ENDPOINT_A);
    TEST_ASSERT_TRUE(handled);
    // POST response only includes remaining credit value, and units
    // {"re": 4294967295, "un": "seconds"}
    uint8_t expected_payload_bytes[] = {
        0xbf, 0x62, 0x72, 0x65, 0x1a, 0xFF, 0xFF, 0xFF, 0xFF, 0x62, 0x75,
        0x6e, 0x67, 0x73, 0x65, 0x63, 0x6f, 0x6e, 0x64, 0x73, 0xff};
    for (uint8_t i = 0; i < response_packet.payload_len; ++i)
    {
        PRINT("%02x ", (uint8_t) * (response_packet.payload + i));
        TEST_ASSERT_EQUAL_UINT(expected_payload_bytes[i],
                               (uint8_t) * (response_packet.payload + i));
    }
    PRINT("\n");

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(CHANGED_2_04, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(21, response_packet.payload_len);

    // GET, confirm device is unlocked
    memset(&request_packet, 0x00, sizeof(request_packet));
    memset(&response_packet, 0x00, sizeof(response_packet));
    memset(&RESP_BUFFER, 0x00, sizeof(RESP_BUFFER));

    // Prepare a GET message with baseline interface
    _internal_set_coap_headers(&request_packet, COAP_TYPE_NON, COAP_GET);

    G_OC_MESSAGE->length =
        coap_serialize_message(&request_packet, G_OC_MESSAGE->data);

    OC_DBG("Requesting GET to '/c' URI with no baseline interface");

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_UNLOCKED);

    handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                               &response_packet,
                                               (void*) &RESP_BUFFER,
                                               &FAKE_ENDPOINT_A);

    TEST_ASSERT_TRUE(handled);

    PRINT("Raw CBOR Payload bytes follow (1):\n");
    // {"mo": 2, "re": 4294967295, "sM": [0, 1, 2, 3], "un": "seconds"}
    uint8_t expected_get_payload_bytes[] = {
        0xbf, 0x62, 0x6d, 0x6f, 0x2,  0x62, 0x72, 0x65, 0x1a, 0xff, 0xff, 0xff,
        0xff, 0x62, 0x73, 0x4d, 0x9f, 0x0,  0x1,  0x2,  0x3,  0xff, 0x62, 0x75,
        0x6e, 0x67, 0x73, 0x65, 0x63, 0x6f, 0x6e, 0x64, 0x73, 0xff};
    for (uint8_t i = 0; i < response_packet.payload_len; ++i)
    {
        PRINT("%02x ", (uint8_t) * (response_packet.payload + i));
        TEST_ASSERT_EQUAL_UINT(expected_get_payload_bytes[i],
                               (uint8_t) * (response_packet.payload + i));
    }
    PRINT("\n");

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(CONTENT_2_05, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(34, response_packet.payload_len);
}
