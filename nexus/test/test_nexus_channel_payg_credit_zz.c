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
#include "src/nexus_cose_mac0_common.h"
#include "src/nexus_cose_mac0_sign.h"
#include "src/nexus_cose_mac0_verify.h"

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
    oc_resource_t* resource = oc_ri_get_app_resource_by_uri(
        "nx/pc", 5, NEXUS_CHANNEL_NEXUS_DEVICE_ID);
    TEST_ASSERT_EQUAL_STRING_LEN("/nx/pc", resource->uri.ptr, 6);
    TEST_ASSERT_EQUAL_STRING_LEN(
        "angaza.com.nx.pc", resource->types.ptr, strlen("angaza.com.nx.pc"));

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
    coap_set_header_uri_path(request_packet, "/nx/pc", strlen("/nx/pc"));
}

void test_payg_credit_init__is_leading_mode__initialize_maintains_credit(void)
{
    // we perform a custom setup for this function, as we want to simulate
    // a link being present before initializing the PAYG credit module
    nexus_channel_core_shutdown();
    oc_nexus_testing_reinit_mmem_lists();
    oc_message_unref(G_OC_MESSAGE);

    nexus_channel_core_init();
    nexus_channel_res_link_hs_init();
    nexus_channel_link_manager_init();

    struct nx_id linked_acc_id = {5921, 123458};

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_create_link(
        &linked_acc_id,
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_CONTROLLER);
    nexus_channel_link_manager_process(0);

    // Is leading device, will not reset credit on boot.
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_ENABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(54021);
    // leading device will not send GET request on boot
    nexus_channel_res_payg_credit_init();

    TEST_ASSERT_EQUAL(54021, _nexus_channel_payg_credit_remaining_credit());
    enum nexus_channel_payg_credit_operating_mode mode =
        _nexus_channel_res_payg_credit_get_credit_operating_mode();
    TEST_ASSERT_EQUAL(NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_LEADING, mode);
}

void test_payg_credit_init__is_relaying_mode__initializes_credit_and_sets_get_requests(
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

    struct nx_id linked_acc_id = {5921, 123458};
    struct nx_id linked_cont_id = {33, 44};

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_create_link(
        &linked_acc_id,
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_CONTROLLER);
    nexus_channel_link_manager_process(0);

    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_create_link(
        &linked_cont_id,
        CHANNEL_LINK_OPERATING_MODE_ACCESSORY,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_ACCESSORY);
    nexus_channel_link_manager_process(0);

    // relaying device will not reset credit on boot
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_ENABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(54021);
    // expected calls due to initial GET on boot
    // Arbitrary 'my_id'
    struct nx_id my_id = {0xFFFF, 0xFAFBFCFD};
    nxp_common_request_processing_Expect();
    nxp_channel_get_nexus_id_ExpectAndReturn(my_id);
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);
    nexus_channel_res_payg_credit_init();

    TEST_ASSERT_EQUAL(54021, _nexus_channel_payg_credit_remaining_credit());
    enum nexus_channel_payg_credit_operating_mode mode =
        _nexus_channel_res_payg_credit_get_credit_operating_mode();
    TEST_ASSERT_EQUAL(NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_RELAYING, mode);
}

void test_payg_credit_init__is_following__not_unlocked__initializes_with_0_credit(
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
    // enter dependent mode. Since it is in dependent / following mode,
    // it will reset the product credit to 0 on boot.
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_ENABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(54021);
    nxp_channel_payg_credit_set_ExpectAndReturn(0, NX_CHANNEL_ERROR_NONE);

    // expected calls due to initial GET on boot
    // Arbitrary 'my_id'
    struct nx_id my_id = {0xFFFF, 0xFAFBFCFD};
    nxp_common_request_processing_Expect();
    nxp_channel_get_nexus_id_ExpectAndReturn(my_id);
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);
    nexus_channel_res_payg_credit_init();

    TEST_ASSERT_EQUAL(0, _nexus_channel_payg_credit_remaining_credit());
    enum nexus_channel_payg_credit_operating_mode mode =
        _nexus_channel_res_payg_credit_get_credit_operating_mode();
    TEST_ASSERT_EQUAL(NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_FOLLOWING, mode);
}

void test_payg_credit_init__is_following__unlocked__initializes_unlocked(void)
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
    // enter dependent mode. Since it is in dependent / following mode,
    // but is unlocked, it will not change product stored PAYG credit on boot.
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_UNLOCKED);

    // expected calls due to initial GET on boot
    // Arbitrary 'my_id'

    struct nx_id my_id = {0xFFFF, 0xFAFBFCFD};
    nxp_common_request_processing_Expect();
    nxp_channel_get_nexus_id_ExpectAndReturn(my_id);
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);
    nexus_channel_res_payg_credit_init();

    TEST_ASSERT_EQUAL(UINT32_MAX,
                      _nexus_channel_payg_credit_remaining_credit());
    enum nexus_channel_payg_credit_operating_mode mode =
        _nexus_channel_res_payg_credit_get_credit_operating_mode();
    TEST_ASSERT_EQUAL(NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_FOLLOWING, mode);
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

    OC_DBG("Requesting GET to '/nx/pc' URI with baseline");

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(86437);
    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_ENDPOINT_A);
    TEST_ASSERT_TRUE(handled);

    PRINT("Raw CBOR Payload bytes follow (1):\n");
    /* {"rt": ["angaza.com.nx.pc"], "if": ["oic.if.rw",
     * "oic.if.baseline"], "mo": 0, "re": 86437, "un": 1, "di": [] (empty
     * array)}
     */
    uint8_t expected_payload_bytes[] = {
        0xbf, 0x62, 0x72, 0x74, 0x9f, 0x70, 0x61, 0x6e, 0x67, 0x61, 0x7a,
        0x61, 0x2e, 0x63, 0x6f, 0x6d, 0x2e, 0x6e, 0x78, 0x2e, 0x70, 0x63,
        0xff, 0x62, 0x69, 0x66, 0x9f, 0x69, 0x6f, 0x69, 0x63, 0x2e, 0x69,
        0x66, 0x2e, 0x72, 0x77, 0x6f, 0x6f, 0x69, 0x63, 0x2e, 0x69, 0x66,
        0x2e, 0x62, 0x61, 0x73, 0x65, 0x6c, 0x69, 0x6e, 0x65, 0xff, 0x62,
        0x6d, 0x6f, 0x00, 0x62, 0x72, 0x65, 0x1a, 0x00, 0x01, 0x51, 0xa5,
        0x62, 0x75, 0x6e, 0x01, 0x62, 0x64, 0x69, 0x9f, 0xff, 0xff};

    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_payload_bytes,
                                  response_packet.payload,
                                  response_packet.payload_len);

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(CONTENT_2_05, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(76, response_packet.payload_len);

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

    OC_DBG("Requesting GET to '/nx/pc' URI with no baseline interface");

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
    // {"mo": 2, "re": 1209600, "un": 1, "di": [] (empty array)}
    uint8_t expected_payload_bytes[] = {
        0xbf, 0x62, 0x6d, 0x6f, 0x02, 0x62, 0x72, 0x65, 0x1a, 0x00, 0x12, 0x75,
        0x00, 0x62, 0x75, 0x6e, 0x01, 0x62, 0x64, 0x69, 0x9f, 0xff, 0xff};

    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_payload_bytes,
                                  response_packet.payload,
                                  response_packet.payload_len);

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(CONTENT_2_05, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(23, response_packet.payload_len);

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
    // {"re": 12345678, "un": 1}
    uint8_t expected_payload_bytes[] = {0xbf,
                                        0x62,
                                        0x72,
                                        0x65,
                                        0x1a,
                                        0x00,
                                        0xbc,
                                        0x61,
                                        0x4e,
                                        0x62,
                                        0x75,
                                        0x6e,
                                        0x01,
                                        0xff};

    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_payload_bytes,
                                  response_packet.payload,
                                  response_packet.payload_len);

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(CHANGED_2_04, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(14, response_packet.payload_len);

    _initialize_oc_rep_pool();
    // ensure that the message is parseable
    int success = oc_parse_rep(response_packet.payload, // payload,
                               response_packet.payload_len,
                               &G_OC_REP);
    TEST_ASSERT_EQUAL(0, success);
}

void test_payg_credit_server_too_long_elapses_with_no_update__credit_resets_to_0(
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

    // Ensure that credit is received and updated
    nxp_channel_payg_credit_set_ExpectAndReturn(12345678,
                                                NX_CHANNEL_ERROR_NONE);
    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_ENDPOINT_A);
    TEST_ASSERT_TRUE(handled);

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_ENABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(12345678);
    // Now, elapse some time, but not enough to erase credit
    uint32_t min_sleep = nexus_channel_res_payg_credit_process(
        NEXUS_CHANNEL_PAYG_CREDIT_FOLLOWER_MAX_TIME_BETWEEN_UPDATES_SECONDS -
        3);
    TEST_ASSERT_EQUAL(12345678, _nexus_channel_payg_credit_remaining_credit());
    // should call process again in 3 seconds to erase credit if no POST update
    // received by then
    TEST_ASSERT_EQUAL(3, min_sleep);

    // Now, elapse enough time to erase credit. Should trigger "set credit = 0"
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_ENABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(12345678);
    nxp_channel_payg_credit_set_ExpectAndReturn(0, NX_CHANNEL_ERROR_NONE);
    // elapse the remaining 3 seconds
    min_sleep = nexus_channel_res_payg_credit_process(3);
    TEST_ASSERT_EQUAL(0, _nexus_channel_payg_credit_remaining_credit());
    // We've reset credit, no need to call again soon
    TEST_ASSERT_EQUAL(NEXUS_COMMON_IDLE_TIME_BETWEEN_PROCESS_CALL_SECONDS,
                      min_sleep);
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
    // {"re": 4294967295, "un": 1}
    uint8_t expected_payload_bytes[] = {0xbf,
                                        0x62,
                                        0x72,
                                        0x65,
                                        0x1a,
                                        0xFF,
                                        0xFF,
                                        0xFF,
                                        0xFF,
                                        0x62,
                                        0x75,
                                        0x6e,
                                        0x01,
                                        0xff};

    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_payload_bytes,
                                  response_packet.payload,
                                  response_packet.payload_len);

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(CHANGED_2_04, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(14, response_packet.payload_len);

    // GET, confirm device is unlocked
    memset(&request_packet, 0x00, sizeof(request_packet));
    memset(&response_packet, 0x00, sizeof(response_packet));
    memset(&RESP_BUFFER, 0x00, sizeof(RESP_BUFFER));

    // Prepare a GET message with baseline interface
    _internal_set_coap_headers(&request_packet, COAP_TYPE_NON, COAP_GET);

    G_OC_MESSAGE->length =
        coap_serialize_message(&request_packet, G_OC_MESSAGE->data);

    OC_DBG("Requesting GET to '/nx/pc' URI with no baseline interface");

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_UNLOCKED);

    handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                               &response_packet,
                                               (void*) &RESP_BUFFER,
                                               &FAKE_ENDPOINT_A);

    TEST_ASSERT_TRUE(handled);

    PRINT("Raw CBOR Payload bytes follow (1):\n");
    // {"mo": 2, "re": 4294967295, "un": 1, "di": [] (empty array)}
    uint8_t expected_get_payload_bytes[] = {
        0xbf, 0x62, 0x6d, 0x6f, 0x02, 0x62, 0x72, 0x65, 0x1a, 0xff, 0xff, 0xff,
        0xff, 0x62, 0x75, 0x6e, 0x01, 0x62, 0x64, 0x69, 0x9f, 0xff, 0xff};
    for (uint8_t i = 0; i < response_packet.payload_len; ++i)
    {
        PRINT("%02x ", (uint8_t) * (response_packet.payload + i));
        TEST_ASSERT_EQUAL_UINT(expected_get_payload_bytes[i],
                               (uint8_t) * (response_packet.payload + i));
    }
    PRINT("\n");

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(CONTENT_2_05, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(23, response_packet.payload_len);
}

void test_payg_credit_client_one_linked_controller__controller_unlocked__handle_invalid_get_response(
    void)
{
    // we want to initialize this test without payg_credit being initialized,
    // as we want to trigger the initial "GET" request
    nexus_channel_core_shutdown();
    oc_nexus_testing_reinit_mmem_lists();

    // happen
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

    // set up a link to another device which is controlling this one
    struct nx_id linked_cont = {44242, 570555388};

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_create_link(
        &linked_cont,
        CHANNEL_LINK_OPERATING_MODE_ACCESSORY,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_ACCESSORY);
    nexus_channel_link_manager_process(0);

    // Cause this device to GET credit from the linked controller. We
    // do this by initializing the PAYG credit resource
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    nxp_common_request_processing_Expect();
    struct nx_id my_id = {0xFFFF, 0xFAFBFCFD};
    nxp_channel_get_nexus_id_ExpectAndReturn(my_id);
    // Empirically, message sent is
    // 51 02 E2 41 40 B2 6E 78 ....
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);
    nexus_channel_res_payg_credit_init();

    OC_DBG("Testing simulated response to PAYG credit GET request");

    // Get endpoint
    // get client CB
    // get response packet
    coap_packet_t resp_packet = {0};
    const uint8_t mid = 2;
    const uint8_t token = 0x40;
    coap_udp_init_message(&resp_packet, COAP_TYPE_NON, CONTENT_2_05, mid);
    coap_set_header_content_format(&resp_packet, APPLICATION_COSE_MAC0);
    coap_set_token(&resp_packet, &token, 1);
    coap_set_header_uri_path(&resp_packet, "nx/pc", 5);

    // not correct keys in response
    // {"credit": 555} (expects 're' key)
    uint8_t resp_data_cbor[] = {
        0xBF, 0x66, 0x63, 0x72, 0x65, 0x64, 0x69, 0x74, 0x19, 0x02, 0x2B, 0xFF};

    coap_set_payload(&resp_packet, resp_data_cbor, sizeof(resp_data_cbor));

    // secure the reply
    const nexus_cose_mac0_common_macparams_t mac_params = {
        &link_key,
        38,
        // aad
        {
            resp_packet.code,
            (uint8_t*) resp_packet.uri_path,
            (uint8_t) resp_packet.uri_path_len,
        },
        resp_data_cbor,
        sizeof(resp_data_cbor),
    };

    uint8_t enc_data[NEXUS_CHANNEL_MAX_CBOR_PAYLOAD_SIZE];
    size_t enc_size;
    nexus_cose_error encode_result = nexus_cose_mac0_sign_encode_message(
        &mac_params, enc_data, sizeof(enc_data), &enc_size);
    TEST_ASSERT_EQUAL(NEXUS_COSE_ERROR_NONE, encode_result);

    coap_set_payload(&resp_packet, enc_data, enc_size);

    // Create a serialized CoAP message so we can simulate receiving it
    // allocate because `oc_network_event` will attempt to unref it later
    TEST_ASSERT_NOT_EQUAL(NULL, G_OC_MESSAGE);
    G_OC_MESSAGE->length =
        coap_serialize_message(&resp_packet, G_OC_MESSAGE->data);
    oc_endpoint_copy(&G_OC_MESSAGE->endpoint, &FAKE_ENDPOINT_A);

    // will call `oc_recv_message` -> `oc_process_post` to network event handler
    // -> `oc_process_post` to coap engine (INBOUND_RI_EVENT)
    // -> `coap_receive(data)`
    // message is then parsed, then the message code is detected. Since its
    // not a request, it will jump to line ~580 in `engine.c` which then
    // finds the client callback by message token,
    // oc_network_event will unref the message, no need to do so here
    oc_network_event(G_OC_MESSAGE);
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    nexus_channel_core_process(0);
    // credit is not updated
    TEST_ASSERT_EQUAL(0, _nexus_channel_payg_credit_remaining_credit());
}

void test_payg_credit_client_one_linked_controller__controller_unlocked__handle_valid_get_response(
    void)
{
    // we want to initialize this test without payg_credit being initialized,
    // as we want to trigger the initial "GET" request
    nexus_channel_core_shutdown();
    oc_nexus_testing_reinit_mmem_lists();

    // happen
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

    // set up a link to another device which is controlling this one
    struct nx_id linked_cont = {44242, 570555388};

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_create_link(
        &linked_cont,
        CHANNEL_LINK_OPERATING_MODE_ACCESSORY,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_ACCESSORY);
    nexus_channel_link_manager_process(0);

    // Cause this device to GET credit from the linked controller. We
    // do this by initializing the PAYG credit resource
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    nxp_common_request_processing_Expect();
    struct nx_id my_id = {0xFFFF, 0xFAFBFCFD};
    nxp_channel_get_nexus_id_ExpectAndReturn(my_id);
    // Empirically, message sent is
    // 51 02 E2 41 40 B2 6E 78 ....
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);
    nexus_channel_res_payg_credit_init();

    OC_DBG("Testing simulated response to PAYG credit GET request");

    // Get endpoint
    // get client CB
    // get response packet
    coap_packet_t resp_packet = {0};
    const uint8_t mid = 2;
    const uint8_t token = 0x40;
    coap_udp_init_message(&resp_packet, COAP_TYPE_NON, CONTENT_2_05, mid);
    coap_set_header_content_format(&resp_packet, APPLICATION_COSE_MAC0);
    coap_set_token(&resp_packet, &token, 1);
    coap_set_header_uri_path(&resp_packet, "nx/pc", 5);

    // has expected credit key in response - will extract and use
    // {"re": 555} (expects 're' key)
    uint8_t resp_data_cbor[] = {0xBF, 0x62, 0x72, 0x65, 0x19, 0x02, 0x2B, 0xFF};

    // secure the reply
    const nexus_cose_mac0_common_macparams_t mac_params = {
        &link_key,
        38,
        // aad
        {
            resp_packet.code,
            (uint8_t*) resp_packet.uri_path,
            (uint8_t) resp_packet.uri_path_len,
        },
        resp_data_cbor,
        sizeof(resp_data_cbor),
    };

    uint8_t enc_data[NEXUS_CHANNEL_MAX_CBOR_PAYLOAD_SIZE];
    size_t enc_size;
    nexus_cose_error encode_result = nexus_cose_mac0_sign_encode_message(
        &mac_params, enc_data, sizeof(enc_data), &enc_size);
    TEST_ASSERT_EQUAL(NEXUS_COSE_ERROR_NONE, encode_result);

    coap_set_payload(&resp_packet, enc_data, enc_size);

    // Create a serialized CoAP message so we can simulate receiving it
    // allocate because `oc_network_event` will attempt to unref it later
    TEST_ASSERT_NOT_EQUAL(NULL, G_OC_MESSAGE);
    G_OC_MESSAGE->length =
        coap_serialize_message(&resp_packet, G_OC_MESSAGE->data);
    oc_endpoint_copy(&G_OC_MESSAGE->endpoint, &FAKE_ENDPOINT_A);

    // will call `oc_recv_message` -> `oc_process_post` to network event handler
    // -> `oc_process_post` to coap engine (INBOUND_RI_EVENT)
    // -> `coap_receive(data)`
    // message is then parsed, then the message code is detected. Since its
    // not a request, it will jump to line ~580 in `engine.c` which then
    // finds the client callback by message token,
    // oc_network_event will unref the message, no need to do so here
    oc_network_event(G_OC_MESSAGE);
    nxp_channel_payg_credit_set_ExpectAndReturn(555, NX_CHANNEL_ERROR_NONE);
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_ENABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(555);
    nexus_channel_core_process(0);
    // credit is updated
    TEST_ASSERT_EQUAL(555, _nexus_channel_payg_credit_remaining_credit());
}

void test_payg_credit_server_get_controller_two_accessories_linked_credit_resource__shows_two_accessory_ids_correctly(
    void)
{
    // set up a link to another device which is controlling this one
    struct nx_id linked_acc_1 = {5921, 54321};
    struct nx_id linked_acc_2 = {5921, 2050};

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_create_link(
        &linked_acc_1,
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_CONTROLLER);
    nexus_channel_link_manager_process(0);

    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_create_link(
        &linked_acc_2,
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_CONTROLLER);
    nexus_channel_link_manager_process(0);

    // Prepare buffers
    coap_packet_t request_packet = {0};
    coap_packet_t response_packet = {0};
    uint8_t RESP_BUFFER[2048] = {0};
    // Must be deallocated at end of test.
    // Allocate on `oc_incoming_buffers`
    TEST_ASSERT_NOT_EQUAL(NULL, G_OC_MESSAGE);

    // GET, confirm device is unlocked
    memset(&request_packet, 0x00, sizeof(request_packet));
    memset(&response_packet, 0x00, sizeof(response_packet));
    memset(&RESP_BUFFER, 0x00, sizeof(RESP_BUFFER));

    // Prepare a GET message with baseline interface
    _internal_set_coap_headers(&request_packet, COAP_TYPE_NON, COAP_GET);

    G_OC_MESSAGE->length =
        coap_serialize_message(&request_packet, G_OC_MESSAGE->data);

    OC_DBG("Requesting GET to '/nx/pc' URI with no baseline interface");

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_UNLOCKED);

    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_ENDPOINT_A);
    TEST_ASSERT_TRUE(handled);

    PRINT("Raw CBOR Payload bytes follow (1):\n");
    // {"mo": 1, "re": 4294967295, "un": 1, "di": [h'17210000D431',
    // h'172100000802']}
    uint8_t expected_get_payload_bytes[] = {
        0xbf, 0x62, 0x6d, 0x6f, 0x01, 0x62, 0x72, 0x65, 0x1a, 0xff,
        0xff, 0xff, 0xff, 0x62, 0x75, 0x6e, 0x01, 0x62, 0x64, 0x69,
        0x9f, 0x46, 0x17, 0x21, 0x00, 0x00, 0xD4, 0x31, 0x46, 0x17,
        0x21, 0x00, 0x00, 0x08, 0x02, 0xFF, 0xFF};

    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_get_payload_bytes,
                                  response_packet.payload,
                                  response_packet.payload_len);

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(CONTENT_2_05, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(37, response_packet.payload_len);

    // Now, delete links, and ensure 'di' is empty
    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_clear_all_links();

    nxp_channel_notify_event_Expect(NXP_CHANNEL_EVENT_LINK_DELETED);
    nxp_channel_notify_event_Expect(NXP_CHANNEL_EVENT_LINK_DELETED);
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_UNLOCKED);
    (void) nexus_channel_core_process(0);

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_UNLOCKED);

    handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                               &response_packet,
                                               (void*) &RESP_BUFFER,
                                               &FAKE_ENDPOINT_A);
    TEST_ASSERT_TRUE(handled);

    PRINT("Raw CBOR Payload bytes follow (1):\n");
    // {"mo": 0, "re": 4294967295, "un": 1, "di": []}
    // Notice mode '0' (independent) is different from above (leading)
    uint8_t expected_get_payload_bytes_no_links[] = {
        0xbf, 0x62, 0x6d, 0x6f, 0x00, 0x62, 0x72, 0x65, 0x1a, 0xff, 0xff, 0xff,
        0xff, 0x62, 0x75, 0x6e, 0x01, 0x62, 0x64, 0x69, 0x9f, 0xff, 0xff};

    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_get_payload_bytes_no_links,
                                  response_packet.payload,
                                  response_packet.payload_len);

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(CONTENT_2_05, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(23, response_packet.payload_len);
}

void test_payg_credit_process__no_links__returns_early(void)
{
    TEST_ASSERT_EQUAL_UINT(0, nx_channel_link_count());
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    uint32_t min_sleep = nexus_channel_res_payg_credit_process(0);
    TEST_ASSERT_EQUAL_UINT(NEXUS_COMMON_IDLE_TIME_BETWEEN_PROCESS_CALL_SECONDS,
                           min_sleep);
}

void test_payg_credit_process__one_linked_accessory__post_and_cycle_intervals_send_timing__ok(
    void)
{
    // we use system uptime in this test, initialize it to 0
    nxp_common_request_processing_Expect();
    nx_common_init(0);

    struct nx_id my_id = {0xFFFF, 0xFAFBFCFD};
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
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_CONTROLLER);
    nexus_channel_link_manager_process(0);

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    // expect in order to send out the outbound request
    nxp_common_request_processing_Expect();
    nxp_channel_get_nexus_id_ExpectAndReturn(my_id);
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);
    uint32_t min_sleep = nexus_channel_core_process(0);
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_CHANNEL_PAYG_CREDIT_INTERVAL_BETWEEN_PAYG_CREDIT_POST_SECONDS,
        min_sleep);

    // call again with 1 second elapsed, 1 second left. No message sent.
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    // The next 'soonest' call time here is the secured message send idle
    // timeout, or 5 seconds (`OC_TRANSACTION_CACHED_IDLE_TIMEOUT_SECONDS`).
    // This is because we haven't elapsed any time since
    // we sent the message above (haven't called `core_process` with a time
    // > 0).
    min_sleep = nexus_channel_core_process(0);
    TEST_ASSERT_EQUAL_UINT(5, min_sleep);

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    min_sleep = nexus_channel_res_payg_credit_process(7);
    // We've only let PAYG credit process know about 7 seconds elapsing since
    // the last message was sent and the cycle was completed,
    // so we expect it to ask to be called again 7 seconds sooner than a full
    // cycle time. We have *not* updated internal system uptime, which is still
    // at 0.
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_CHANNEL_PAYG_CREDIT_POST_UPDATE_CYCLE_TIME_SECONDS - 7,
        min_sleep);

    // after waiting for an entire cycle time, we resend a message
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    // `nx_common_process` is necessary to update the system uptime. This
    // will ensure that we time out the secured message idle timeout, and
    // so the soonest callback should be defined by PAYG credit.
    // Time elapsed from payg credit perspective:
    //  (NEXUS_CHANNEL_PAYG_CREDIT_POST_UPDATE_CYCLE_TIME_SECONDS - 7 seconds -
    //  15 seconds).
    min_sleep = nx_common_process(15);
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_CHANNEL_PAYG_CREDIT_POST_UPDATE_CYCLE_TIME_SECONDS - (15 + 7),
        min_sleep);

    // Wait 8 more seconds. We will attempt to send the message out, then
    // request processing after the "POST INTERVAL"
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    // expect in order to send out the outbound request
    nxp_common_request_processing_Expect();
    nxp_channel_get_nexus_id_ExpectAndReturn(my_id);
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);
    min_sleep = nexus_channel_core_process(8);
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_CHANNEL_PAYG_CREDIT_INTERVAL_BETWEEN_PAYG_CREDIT_POST_SECONDS,
        min_sleep);

    // Finally, we expect to see another inter-cycle wait period
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    // We just call this function
    // to ensure that there aren't any unexpected 'outbound' messages being
    // sent (they would be sent by calling `nexus_channel_core_process`).
    (void) nexus_channel_core_process(0);

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    min_sleep = nexus_channel_res_payg_credit_process(1);
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_CHANNEL_PAYG_CREDIT_POST_UPDATE_CYCLE_TIME_SECONDS - 1,
        min_sleep);
}

void test_payg_credit_process__one_linked_accessory__payg_state_transitions__immediately_restarts_cycle(
    void)
{
    // we use system uptime in this test, initialize it to 0
    nxp_common_request_processing_Expect();
    nx_common_init(0);

    struct nx_id my_id = {0xFFFF, 0xFAFBFCFD};
    // set up a link to another device which is an accessory to this one
    struct nx_id linked_acc_id = {5921, 123458};

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_create_link(
        &linked_acc_id,
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_CONTROLLER);
    nexus_channel_link_manager_process(0);

    // Process, should send the outbound PAYG credit update to the linked
    // controller
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    // expect in order to send out the outbound request
    nxp_common_request_processing_Expect();
    nxp_channel_get_nexus_id_ExpectAndReturn(my_id);
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);
    uint32_t min_sleep = nx_common_process(0);
    // after sending message, PAYG credit asks to be called again after
    // POST interval
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_CHANNEL_PAYG_CREDIT_INTERVAL_BETWEEN_PAYG_CREDIT_POST_SECONDS,
        min_sleep);

    // seconds between nx_common_process calls. Picked to be arbitrarily
    // long enough to elapse any timeouts (secure message timeout, e.g.)
    static uint32_t NX_COMMON_PROCESS_TEST_UPTIME_INTERVAL = 20;

    // Confirm PAYG credit is in idle cycle, no change to PAYG state
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    // 20 > secured message timeout and POST credit interval
    (void) nx_common_process(NX_COMMON_PROCESS_TEST_UPTIME_INTERVAL);

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    min_sleep = nexus_channel_res_payg_credit_process(0);
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_CHANNEL_PAYG_CREDIT_POST_UPDATE_CYCLE_TIME_SECONDS - 20,
        min_sleep);

    // Changed from DISABLED->ENABLED, PAYG credit should attempt to send again
    // regardless of actual time elapsed
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_ENABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(1);
    nxp_common_request_processing_Expect();
    nxp_channel_get_nexus_id_ExpectAndReturn(my_id);
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);
    min_sleep = nexus_channel_res_payg_credit_process(0);
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_CHANNEL_PAYG_CREDIT_INTERVAL_BETWEEN_PAYG_CREDIT_POST_SECONDS,
        min_sleep);

    // clear out secured message timeout, confirm PAYG credit is idle again
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_ENABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(1);
    (void) nx_common_process(NX_COMMON_PROCESS_TEST_UPTIME_INTERVAL * 2);

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_ENABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(1);
    min_sleep = nexus_channel_res_payg_credit_process(0);
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_CHANNEL_PAYG_CREDIT_POST_UPDATE_CYCLE_TIME_SECONDS - 20,
        min_sleep);

    // Changed from ENABLED->UNLOCKED, PAYG credit should attempt to send again
    // regardless of actual time elapsed
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_UNLOCKED);
    // No call to get credit if we are unlocked
    // nxp_common_payg_credit_get_remaining_ExpectAndReturn(0xFFFFFFFF);
    nxp_common_request_processing_Expect();
    nxp_channel_get_nexus_id_ExpectAndReturn(my_id);
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);
    min_sleep = nexus_channel_res_payg_credit_process(0);
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_CHANNEL_PAYG_CREDIT_INTERVAL_BETWEEN_PAYG_CREDIT_POST_SECONDS,
        min_sleep);

    // clear out secured message timeout, confirm PAYG credit is idle again
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_UNLOCKED);
    // nxp_common_payg_credit_get_remaining_ExpectAndReturn(1);
    (void) nx_common_process(NX_COMMON_PROCESS_TEST_UPTIME_INTERVAL * 3);

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_UNLOCKED);
    // nxp_common_payg_credit_get_remaining_ExpectAndReturn(1);
    min_sleep = nexus_channel_res_payg_credit_process(0);
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_CHANNEL_PAYG_CREDIT_POST_UPDATE_CYCLE_TIME_SECONDS - 20,
        min_sleep);

    // Changed from UNLOCKED->ENABLED, PAYG credit should attempt to send again
    // regardless of actual time elapsed
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_ENABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(72000);
    nxp_common_request_processing_Expect();
    nxp_channel_get_nexus_id_ExpectAndReturn(my_id);
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);
    min_sleep = nexus_channel_res_payg_credit_process(0);
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_CHANNEL_PAYG_CREDIT_INTERVAL_BETWEEN_PAYG_CREDIT_POST_SECONDS,
        min_sleep);

    // clear out secured message timeout, confirm PAYG credit is idle again
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_ENABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(72000);
    (void) nx_common_process(NX_COMMON_PROCESS_TEST_UPTIME_INTERVAL * 4);

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_ENABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(72000);
    min_sleep = nexus_channel_res_payg_credit_process(0);
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_CHANNEL_PAYG_CREDIT_POST_UPDATE_CYCLE_TIME_SECONDS - 20,
        min_sleep);

    // Changed from ENABLED->DISABLED, PAYG credit should attempt to send again
    // regardless of actual time elapsed
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    nxp_common_request_processing_Expect();
    nxp_channel_get_nexus_id_ExpectAndReturn(my_id);
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);
    min_sleep = nexus_channel_res_payg_credit_process(0);
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_CHANNEL_PAYG_CREDIT_INTERVAL_BETWEEN_PAYG_CREDIT_POST_SECONDS,
        min_sleep);

    // clear out secured message timeout, confirm PAYG credit is idle again
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    (void) nx_common_process(NX_COMMON_PROCESS_TEST_UPTIME_INTERVAL * 5);

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    min_sleep = nexus_channel_res_payg_credit_process(0);
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_CHANNEL_PAYG_CREDIT_POST_UPDATE_CYCLE_TIME_SECONDS - 20,
        min_sleep);

    // Changed from DISABLED->UNLOCKED, PAYG credit should attempt to send again
    // regardless of actual time elapsed
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_UNLOCKED);
    // nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    nxp_common_request_processing_Expect();
    nxp_channel_get_nexus_id_ExpectAndReturn(my_id);
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);
    min_sleep = nexus_channel_res_payg_credit_process(0);
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_CHANNEL_PAYG_CREDIT_INTERVAL_BETWEEN_PAYG_CREDIT_POST_SECONDS,
        min_sleep);

    // clear out secured message timeout, confirm PAYG credit is idle again
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_UNLOCKED);
    // nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    (void) nx_common_process(NX_COMMON_PROCESS_TEST_UPTIME_INTERVAL * 6);

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_UNLOCKED);
    // nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    min_sleep = nexus_channel_res_payg_credit_process(0);
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_CHANNEL_PAYG_CREDIT_POST_UPDATE_CYCLE_TIME_SECONDS - 20,
        min_sleep);

    // Changed from UNLOCKED->DISABLED, PAYG credit should attempt to send again
    // regardless of actual time elapsed
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    nxp_common_request_processing_Expect();
    nxp_channel_get_nexus_id_ExpectAndReturn(my_id);
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);
    min_sleep = nexus_channel_res_payg_credit_process(0);
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_CHANNEL_PAYG_CREDIT_INTERVAL_BETWEEN_PAYG_CREDIT_POST_SECONDS,
        min_sleep);

    // clear out secured message timeout, confirm PAYG credit is idle again
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    (void) nx_common_process(NX_COMMON_PROCESS_TEST_UPTIME_INTERVAL * 7);

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    min_sleep = nexus_channel_res_payg_credit_process(0);
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_CHANNEL_PAYG_CREDIT_POST_UPDATE_CYCLE_TIME_SECONDS - 20,
        min_sleep);
}

void test_payg_credit_process__two_linked_accessories_one_linked_controller__post_and_cycle_interval_timing__ok(
    void)
{
    struct nx_id my_id = {0xFFFF, 0xFAFBFCFD};
    struct nx_id linked_acc_1 = {5921, 1};
    struct nx_id linked_acc_2 = {5921, 2};
    struct nx_id linked_cont = {5921, 3};

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    // Set up three links
    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_create_link(
        &linked_acc_1,
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_CONTROLLER);
    nexus_channel_link_manager_process(0);

    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_create_link(
        &linked_cont,
        CHANNEL_LINK_OPERATING_MODE_ACCESSORY,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_ACCESSORY);
    nexus_channel_link_manager_process(0);

    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_create_link(
        &linked_acc_2,
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_CONTROLLER);
    // For the last link, just directly call `nexus_channel_core_process`,
    // kicking off the PAYG credit code as well
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    // expect in order to send out the outbound request (FIRST POST request
    // sent here)
    nxp_common_request_processing_Expect();
    nxp_channel_get_nexus_id_ExpectAndReturn(my_id);
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);
    uint32_t min_sleep = nexus_channel_core_process(0);
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_CHANNEL_PAYG_CREDIT_INTERVAL_BETWEEN_PAYG_CREDIT_POST_SECONDS,
        min_sleep);

    // additional `core_process`calls with 0 time elapsed don't trigger
    // any further sent messages without more time elapsing
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    (void) nexus_channel_core_process(0);

    // Now, elapse the time between sending POST messages - should send another
    // message, and time for the next interval
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    // expect in order to send out the outbound request (SECOND POST request
    // sent here)
    nxp_common_request_processing_Expect();
    nxp_channel_get_nexus_id_ExpectAndReturn(my_id);
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);
    min_sleep = nexus_channel_core_process(
        NEXUS_CHANNEL_PAYG_CREDIT_INTERVAL_BETWEEN_PAYG_CREDIT_POST_SECONDS);
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_CHANNEL_PAYG_CREDIT_INTERVAL_BETWEEN_PAYG_CREDIT_POST_SECONDS,
        min_sleep);

    // Now, elapse the time between sending POST messages - we've sent two
    // POST messages total, and don't expect any more. PAYG credit asked to
    // be called again in 2 seconds or sooner (from above), and we do that here.
    // This will put it into the idle state where it waits until the next cycle
    // to send another POST request
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    (void) nexus_channel_core_process(
        NEXUS_CHANNEL_PAYG_CREDIT_INTERVAL_BETWEEN_PAYG_CREDIT_POST_SECONDS);

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    min_sleep = nexus_channel_res_payg_credit_process(0);
    // 2 POST cycles elapsed since we *started* the last cycle, we wait that
    // long until sending again
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_CHANNEL_PAYG_CREDIT_POST_UPDATE_CYCLE_TIME_SECONDS -
            (2 *
             NEXUS_CHANNEL_PAYG_CREDIT_INTERVAL_BETWEEN_PAYG_CREDIT_POST_SECONDS),
        min_sleep);
}

void test_payg_credit_process__two_linked_accessories__delete_an_accessory__attempts_to_send_to_only_one(
    void)
{
    struct nx_id my_id = {0xFFFF, 0xFAFBFCFD};
    struct nx_id linked_acc_1 = {5921, 1};
    struct nx_id linked_acc_2 = {5921, 2};

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    // Set up two links
    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_create_link(
        &linked_acc_1,
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_CONTROLLER);
    nexus_channel_link_manager_process(0);

    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_create_link(
        &linked_acc_2,
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_CONTROLLER);
    // For the last link, just directly call `nexus_channel_core_process`,
    // kicking off the PAYG credit code as well
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    // expect in order to send out the outbound request (FIRST POST request
    // sent here)
    nxp_common_request_processing_Expect();
    nxp_channel_get_nexus_id_ExpectAndReturn(my_id);
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);
    uint32_t min_sleep = nexus_channel_core_process(0);
    TEST_ASSERT_EQUAL_UINT(
        NEXUS_CHANNEL_PAYG_CREDIT_INTERVAL_BETWEEN_PAYG_CREDIT_POST_SECONDS,
        min_sleep);

    // additional `core_process`calls with 0 time elapsed don't trigger
    // any further sent messages without more time elapsing
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    (void) nexus_channel_core_process(0);

    // erase all links
    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_clear_all_links();

    // Now, elapse the time between sending POST messages - should *not*
    // send a second post, since we've cleared the links
    nxp_channel_notify_event_Expect(NXP_CHANNEL_EVENT_LINK_DELETED);
    nxp_channel_notify_event_Expect(NXP_CHANNEL_EVENT_LINK_DELETED);
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);

    (void) nexus_channel_core_process(
        NEXUS_CHANNEL_PAYG_CREDIT_INTERVAL_BETWEEN_PAYG_CREDIT_POST_SECONDS);

    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_DISABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(0);
    min_sleep = nexus_channel_res_payg_credit_process(0);

    // should be the idle time - no links, nothing to do/update.
    TEST_ASSERT_EQUAL_UINT(NEXUS_COMMON_IDLE_TIME_BETWEEN_PROCESS_CALL_SECONDS,
                           min_sleep);
}
