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
#include "oc/port/oc_connectivity.h"
#include "oc/util/oc_etimer.h"
#include "oc/util/oc_mmem.h"
#include "oc/util/oc_process.h"
#include "oc/util/oc_timer.h"

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
// Global message that can be allocated and deallocated at start and end
// of tests regardless of failures
static oc_message_t* G_OC_MESSAGE;
static oc_rep_t* G_OC_REP;

oc_endpoint_t FAKE_ENDPOINT = {0};

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
    nexus_channel_link_manager_init();

    oc_resource_t* resource =
        oc_ri_get_app_resource_by_uri("l", 1, NEXUS_CHANNEL_NEXUS_DEVICE_ID);
    TEST_ASSERT_EQUAL_STRING_LEN("/l", resource->uri.ptr, 2);
    TEST_ASSERT_EQUAL_STRING_LEN("angaza.com.nexus.link",
                                 resource->types.ptr,
                                 strlen("angaza.com.nexus.link"));

    // will prepare CoAP engine to send/receive messages
    coap_init_engine();

    // must be deallocated at end of test
    G_OC_MESSAGE = oc_allocate_message();
    G_OC_REP = 0;
    OC_DBG("------ SETUP FINISHED, BEGINNING TEST ------");
}

// Teardown (called after any 'test_*' function is called, automatically)
void tearDown(void)
{
    OC_DBG("------ RUNNING TEARDOWN, END OF TEST ------");

    // "G_OC_MESSAGE" is allocated on each test run in step
    oc_message_unref(G_OC_MESSAGE);
    // some tests *may* call oc_parse_rep, oc_free_rep handles this case
    // We null-check to make sure a test actually allocated G_OC_REP, otherwise
    // we won't be able to free it.
    if (G_OC_REP != 0)
    {
        oc_free_rep(G_OC_REP);
    }
    nexus_channel_core_shutdown();
}

void _internal_set_coap_headers(coap_packet_t* request_packet,
                                coap_message_type_t coap_type,
                                uint8_t coap_code)
{
    coap_udp_init_message(request_packet, coap_type, coap_code, 123);
    coap_set_header_uri_path(request_packet, "/l", strlen("/l"));
}

void test_link_manager_create_link__link_is_created_ok(void)
{
    // initializes with no links present
    struct nx_id linked_id = {0};
    linked_id.authority_id = 5921;
    linked_id.device_id = 123456;

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    nexus_channel_link_t result_link = {0};

    // first, link doesn't exist
    bool result =
        _nexus_channel_link_manager_link_from_nxid(&linked_id, &result_link);
    TEST_ASSERT_FALSE(result);

    nxp_common_request_processing_Expect();
    result = nexus_channel_link_manager_create_link(
        &linked_id,
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    TEST_ASSERT_TRUE(result);

    result =
        _nexus_channel_link_manager_link_from_nxid(&linked_id, &result_link);
    // need to call `process` to create the link
    TEST_ASSERT_FALSE(result);

    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_CONTROLLER);
    nexus_channel_link_manager_process(0);
    result =
        _nexus_channel_link_manager_link_from_nxid(&linked_id, &result_link);
    TEST_ASSERT_TRUE(result);

    TEST_ASSERT_EQUAL_MEMORY(
        &linked_id, &result_link.linked_device_id, sizeof(struct nx_id));
    TEST_ASSERT_EQUAL_MEMORY(&link_key,
                             &result_link.security_data.mode0.sym_key,
                             sizeof(struct nx_common_check_key));
    TEST_ASSERT_EQUAL(sec_data.mode0.nonce,
                      result_link.security_data.mode0.nonce);
    TEST_ASSERT_EQUAL(CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
                      result_link.operating_mode);
    TEST_ASSERT_EQUAL(
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        result_link.security_mode);
}

void test_link_manager_create_link_then_delete__single_link_deleted_ok(void)
{
    // initializes with no links present
    struct nx_id linked_id = {0};
    linked_id.authority_id = 5921;
    linked_id.device_id = 123456;

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    nexus_channel_link_t result_link = {0};

    // first, link doesn't exist
    bool result =
        _nexus_channel_link_manager_link_from_nxid(&linked_id, &result_link);
    TEST_ASSERT_FALSE(result);

    nxp_common_request_processing_Expect();
    result = nexus_channel_link_manager_create_link(
        &linked_id,
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    TEST_ASSERT_TRUE(result);

    result =
        _nexus_channel_link_manager_link_from_nxid(&linked_id, &result_link);
    // need to call `process` to create the link
    TEST_ASSERT_FALSE(result);

    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_CONTROLLER);
    nexus_channel_link_manager_process(0);
    result =
        _nexus_channel_link_manager_link_from_nxid(&linked_id, &result_link);
    TEST_ASSERT_TRUE(result);

    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_clear_all_links();
    nxp_channel_notify_event_Expect(NXP_CHANNEL_EVENT_LINK_DELETED);
    nexus_channel_link_manager_process(0);
    // link is no longer present
    result =
        _nexus_channel_link_manager_link_from_nxid(&linked_id, &result_link);
    TEST_ASSERT_FALSE(result);
}

void test_link_manager_create_multiple_links_delete__both_deleted(void)
{
    // initializes with no links present
    struct nx_id linked_id = {0};
    linked_id.authority_id = 5921;
    linked_id.device_id = 123456;

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    nexus_channel_link_t result_link = {0};

    // first, link doesn't exist
    bool result =
        _nexus_channel_link_manager_link_from_nxid(&linked_id, &result_link);
    TEST_ASSERT_FALSE(result);

    nxp_common_request_processing_Expect();
    result = nexus_channel_link_manager_create_link(
        &linked_id,
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    TEST_ASSERT_TRUE(result);

    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_CONTROLLER);
    nexus_channel_link_manager_process(0);
    result =
        _nexus_channel_link_manager_link_from_nxid(&linked_id, &result_link);
    TEST_ASSERT_TRUE(result);

    // now, create a second link
    struct nx_id linked_id_2 = {0x1234, 555555};
    nxp_common_request_processing_Expect();
    result = nexus_channel_link_manager_create_link(
        &linked_id_2,
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    TEST_ASSERT_TRUE(result);

    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_CONTROLLER);
    nexus_channel_link_manager_process(0);
    result =
        _nexus_channel_link_manager_link_from_nxid(&linked_id_2, &result_link);
    TEST_ASSERT_TRUE(result);

    // now, clear both links
    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_clear_all_links();
    nxp_channel_notify_event_Expect(NXP_CHANNEL_EVENT_LINK_DELETED);
    nxp_channel_notify_event_Expect(NXP_CHANNEL_EVENT_LINK_DELETED);
    nexus_channel_link_manager_process(0);
    // neither link is longer present
    result =
        _nexus_channel_link_manager_link_from_nxid(&linked_id, &result_link);
    TEST_ASSERT_FALSE(result);
    result =
        _nexus_channel_link_manager_link_from_nxid(&linked_id_2, &result_link);
    TEST_ASSERT_FALSE(result);
}

void test_link_manager_create_link_no_process_call__second_create_link_fails(
    void)
{
    // initializes with no links present
    struct nx_id linked_id = {0};
    linked_id.authority_id = 5921;
    linked_id.device_id = 123456;

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    nxp_common_request_processing_Expect();
    bool result = nexus_channel_link_manager_create_link(
        &linked_id,
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    TEST_ASSERT_TRUE(result);

    // try to buffer creating another link without calling `process`, should
    // fail

    result = nexus_channel_link_manager_create_link(
        &linked_id,
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    TEST_ASSERT_FALSE(result);
}

void test_link_manager_create_identical_link__create_link_fails(void)
{
    // initializes with no links present
    struct nx_id linked_id = {0};
    linked_id.authority_id = 5921;
    linked_id.device_id = 123456;

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    nxp_common_request_processing_Expect();
    bool result = nexus_channel_link_manager_create_link(
        &linked_id,
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    // first link is added to queue and will be created on next `process`
    TEST_ASSERT_TRUE(result);
    // actually create the link
    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_CONTROLLER);
    nexus_channel_link_manager_process(0);

    // creating a link to a device which is already linked will fail.
    result = nexus_channel_link_manager_create_link(
        &linked_id,
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    // first link is created OK
    TEST_ASSERT_FALSE(result);
}

void test_link_manager_create_link__exceeded_link_limit__return_false(void)
{
    // initializes with no links present
    struct nx_id linked_id = {0};
    linked_id.authority_id = 5921;
    linked_id.device_id = 123456;

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    nexus_channel_link_t result_link = {0};

    // first, no links exist
    bool result =
        _nexus_channel_link_manager_link_from_nxid(&linked_id, &result_link);
    TEST_ASSERT_FALSE(result);

    for (uint8_t i = 0; i < NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS; i++)
    {
        nxp_common_request_processing_Expect();
        // increment device ID each time so we don't attempt to create an
        // identical link
        linked_id.device_id++;
        result = nexus_channel_link_manager_create_link(
            &linked_id,
            CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
            NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
            &sec_data);

        nxp_channel_notify_event_Expect(
            NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_CONTROLLER);
        nexus_channel_link_manager_process(0);
        TEST_ASSERT_TRUE(result);
    }

    // cannot create more links than MAX_SIMULTANEOUS
    // does not expect to call `nxp_common_request_processing_Expect`
    result = nexus_channel_link_manager_create_link(
        &linked_id,
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    TEST_ASSERT_FALSE(result);
}

void test_link_manager__security_data_from_nxid__no_data_present__returns_false(
    void)
{
    // initializes with no links present
    struct nx_id linked_id = {0};
    linked_id.authority_id = 5921;
    linked_id.device_id = 123456;

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    struct nexus_channel_link_security_mode0_data link_mode0_data;
    memset(&link_mode0_data,
           0xDE,
           sizeof(struct nexus_channel_link_security_mode0_data));
    bool result = nexus_channel_link_manager_security_data_from_nxid(
        &linked_id, &link_mode0_data);
    TEST_ASSERT_FALSE(result);

    // struct unmodified if no data is found
    TEST_ASSERT_EACH_EQUAL_UINT8(
        0xDE,
        (uint8_t*) &link_mode0_data,
        sizeof(struct nexus_channel_link_security_mode0_data));
}

void test_link_manager__security_data_from_nxid__data_present__returns_correct_data(
    void)
{
    // initializes with no links present
    struct nx_id linked_id = {0};
    linked_id.authority_id = 5921;
    linked_id.device_id = 123456;

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_create_link(
        &linked_id,
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);

    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_CONTROLLER);
    nexus_channel_link_manager_process(0);

    struct nexus_channel_link_security_mode0_data link_mode0_data;
    bool result = nexus_channel_link_manager_security_data_from_nxid(
        &linked_id, &link_mode0_data);
    TEST_ASSERT_TRUE(result);

    TEST_ASSERT_EQUAL(sec_data.mode0.nonce, link_mode0_data.nonce);
    TEST_ASSERT_EQUAL_MEMORY(&sec_data.mode0.sym_key,
                             &link_mode0_data.sym_key,
                             sizeof(struct nx_common_check_key));
}

void test_link_manager__increment_security_data_mode0__nonce_updated(void)
{
    // initializes with no links present
    struct nx_id linked_id = {0};
    linked_id.authority_id = 5921;
    linked_id.device_id = 123456;

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    // create a link
    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_create_link(
        &linked_id,
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);

    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_CONTROLLER);
    nexus_channel_link_manager_process(0);

    struct nexus_channel_link_security_mode0_data link_mode0_data;
    bool result = nexus_channel_link_manager_security_data_from_nxid(
        &linked_id, &link_mode0_data);
    TEST_ASSERT_TRUE(result);

    // initialized explicitly as 5 from above
    TEST_ASSERT_EQUAL(5, link_mode0_data.nonce);

    nexus_channel_link_manager_set_security_data_auth_nonce(&linked_id, 20);
    result = nexus_channel_link_manager_security_data_from_nxid(
        &linked_id, &link_mode0_data);
    TEST_ASSERT_TRUE(result);

    TEST_ASSERT_EQUAL(20, link_mode0_data.nonce);
}

void test_res_link_hs_server_get_response__no_link_exists__cbor_data_model_correct(
    void)
{

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

    OC_DBG("Requesting GET to '/l' URI");

    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_ENDPOINT);
    TEST_ASSERT_TRUE(handled);

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(CONTENT_2_05, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(9, response_packet.payload_len);

    PRINT("Raw CBOR Payload bytes follow (1):\n");
    /* Empty 'reps' array `{"reps": []}`
     *
     * BF             # map(*)
     *  64          # text(4)
     *      72657073 # "reps"
     *  9F          # array(*)
     *      FF       # primitive(*)
     *  FF          # primitive(*)
     */
    uint8_t expected_empty_payload[9] = {
        0xBF, 0x64, 0x72, 0x65, 0x70, 0x73, 0x9F, 0xFF, 0xFF};
    for (uint8_t i = 0; i < response_packet.payload_len; ++i)
    {
        PRINT("%02x ", (uint8_t) * (response_packet.payload + i));
        TEST_ASSERT_EQUAL_UINT(expected_empty_payload[i],
                               (uint8_t) * (response_packet.payload + i));
    }
    PRINT("\n");

    _initialize_oc_rep_pool();
    int result = oc_parse_rep(response_packet.payload, // payload,
                              response_packet.payload_len,
                              &G_OC_REP);

    TEST_ASSERT_EQUAL(0, result);
}

void test_res_link_hs_server_get_response__one_link_exists__cbor_correct_oc_parses_ok(
    void)
{
    // set up a single link
    struct nx_id linked_id = {0};
    // big endian = 0x1721
    linked_id.authority_id = 5921;
    // big endian = 0x0001e240
    linked_id.device_id = 123456;

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    nxp_common_request_processing_Expect();
    bool link_created = nexus_channel_link_manager_create_link(
        &linked_id,
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    // link will be established once we call `process`
    TEST_ASSERT_TRUE(link_created);

    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_CONTROLLER);
    nexus_channel_link_manager_process(0);

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

    OC_DBG("Requesting GET to '/l' URI");

    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_ENDPOINT);
    TEST_ASSERT_TRUE(handled);

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(CONTENT_2_05, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(45, response_packet.payload_len);

    PRINT("Raw CBOR Payload bytes follow (1):\n");
    /* {"reps": [{"lD": h'17210001e240', "oM": 1, "sM": 0, "tI": 0, "tA": 0,
     * "tT": 7776000}]}
     */
    uint8_t expected_single_link_payload[45] = {
        0xbf, 0x64, 0x72, 0x65, 0x70, 0x73, 0x9f, 0xbf, 0x62, 0x6c, 0x44, 0x46,
        0x17, 0x21, 0x00, 0x01, 0xE2, 0x40, 0x62, 0x6f, 0x4d, 0x1,  0x62, 0x73,
        0x4d, 0x0,  0x62, 0x74, 0x49, 0x0,  0x62, 0x74, 0x41, 0x0,  0x62, 0x74,
        0x54, 0x1a, 0x0,  0x76, 0xa7, 0x0,  0xff, 0xff, 0xff};

    for (uint8_t i = 0; i < response_packet.payload_len; ++i)
    {
        PRINT("%02x ", (uint8_t) * (response_packet.payload + i));
        TEST_ASSERT_EQUAL_UINT(expected_single_link_payload[i],
                               (uint8_t) * (response_packet.payload + i));
    }

    PRINT("\n");

    _initialize_oc_rep_pool();
    int result = oc_parse_rep(response_packet.payload, // payload,
                              response_packet.payload_len,
                              &G_OC_REP);

    TEST_ASSERT_EQUAL(0, result);
}

void test_nexus_channel_link_manager_index_to_nv_block__cases_8_9_return_true(
    void)
{
    struct nx_common_nv_block_meta* tmp_block_meta = 0;
    bool result =
        _nexus_channel_link_manager_index_to_nv_block(8, &tmp_block_meta);
    TEST_ASSERT_TRUE(result);
    result = _nexus_channel_link_manager_index_to_nv_block(9, &tmp_block_meta);
    TEST_ASSERT_TRUE(result);
}

void test_nexus_channel_link_manager_index_to_nv_block__cases_out_of_range__returns_false(
    void)
{
    struct nx_common_nv_block_meta* tmp_block_meta = 0;
    bool result =
        _nexus_channel_link_manager_index_to_nv_block(10, &tmp_block_meta);
    TEST_ASSERT_FALSE(result);
    result =
        _nexus_channel_link_manager_index_to_nv_block(200, &tmp_block_meta);
    TEST_ASSERT_FALSE(result);
}

void test_res_link_hs_server_get_response_baseline_query__one_link_exists__cbor_correct_oc_parses_ok(
    void)
{
    // set up a single link
    struct nx_id linked_id = {0};
    linked_id.authority_id = 5921;
    linked_id.device_id = 123456;

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    nxp_common_request_processing_Expect();
    bool link_created = nexus_channel_link_manager_create_link(
        &linked_id,
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    // link will be established once we call `process`
    TEST_ASSERT_TRUE(link_created);

    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_CONTROLLER);
    nexus_channel_link_manager_process(0);

    // Prepare buffers
    coap_packet_t request_packet = {0};
    coap_packet_t response_packet = {0};
    uint8_t RESP_BUFFER[2048] = {0};
    // Must be deallocated at end of test.
    // Allocated on `oc_incoming_buffers` by setUp
    TEST_ASSERT_NOT_EQUAL(NULL, G_OC_MESSAGE);

    // Prepare a GET message with baseline query
    _internal_set_coap_headers(&request_packet, COAP_TYPE_NON, COAP_GET);
    char* baseline_query_str = "if=oic.if.baseline\0";
    coap_set_header_uri_query(&request_packet, baseline_query_str);

    G_OC_MESSAGE->length =
        coap_serialize_message(&request_packet, G_OC_MESSAGE->data);

    OC_DBG("Requesting GET to '/l' URI");

    bool handled = oc_ri_invoke_coap_entity_handler(&request_packet,
                                                    &response_packet,
                                                    (void*) &RESP_BUFFER,
                                                    &FAKE_ENDPOINT);
    TEST_ASSERT_TRUE(handled);

    // Check response code and content
    TEST_ASSERT_EQUAL_UINT(CONTENT_2_05, response_packet.code);
    TEST_ASSERT_EQUAL_UINT(103, response_packet.payload_len);

    PRINT("Raw CBOR Payload bytes follow (1):\n");
    /* {"rt": ["angaza.com.nexus.link"], "if": ["oic.if.rw", "oic.if.baseline"],
     * "reps": [{"lD": h'17210001E240', "oM": 1, "sM": 0, "tI": 0, "tA": 0,
     * "tT": 7776000}]}
     */
    uint8_t expected_single_link_payload[103] = {
        0xbf, 0x62, 0x72, 0x74, 0x9f, 0x75, 0x61, 0x6e, 0x67, 0x61, 0x7a, 0x61,
        0x2e, 0x63, 0x6f, 0x6d, 0x2e, 0x6e, 0x65, 0x78, 0x75, 0x73, 0x2e, 0x6c,
        0x69, 0x6e, 0x6b, 0xff, 0x62, 0x69, 0x66, 0x9f, 0x69, 0x6f, 0x69, 0x63,
        0x2e, 0x69, 0x66, 0x2e, 0x72, 0x77, 0x6f, 0x6f, 0x69, 0x63, 0x2e, 0x69,
        0x66, 0x2e, 0x62, 0x61, 0x73, 0x65, 0x6c, 0x69, 0x6e, 0x65, 0xff, 0x64,
        0x72, 0x65, 0x70, 0x73, 0x9f, 0xbf, 0x62, 0x6c, 0x44, 0x46, 0x17, 0x21,
        0x00, 0x01, 0xE2, 0x40, 0x62, 0x6f, 0x4d, 0x1,  0x62, 0x73, 0x4d, 0x0,
        0x62, 0x74, 0x49, 0x0,  0x62, 0x74, 0x41, 0x0,  0x62, 0x74, 0x54, 0x1a,
        0x0,  0x76, 0xa7, 0x0,  0xff, 0xff, 0xff};

    for (uint8_t i = 0; i < response_packet.payload_len; ++i)
    {
        PRINT("%02x ", (uint8_t) * (response_packet.payload + i));
        TEST_ASSERT_EQUAL_UINT(expected_single_link_payload[i],
                               (uint8_t) * (response_packet.payload + i));
    }

    PRINT("\n");

    _initialize_oc_rep_pool();
    int result = oc_parse_rep(response_packet.payload, // payload,
                              response_packet.payload_len,
                              &G_OC_REP);

    TEST_ASSERT_EQUAL(0, result);
}

void test_link_manager_reset_link_secs_since_active__reset_ok(void)
{
    // initializes with no links present
    struct nx_id linked_id = {0};
    linked_id.authority_id = 5921;
    linked_id.device_id = 123456;

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    nexus_channel_link_t result_link = {0};

    TEST_ASSERT_EQUAL(
        UINT32_MAX,
        _nexus_channel_link_manager_secs_since_link_active(&linked_id));

    // first, link doesn't exist
    bool result =
        _nexus_channel_link_manager_link_from_nxid(&linked_id, &result_link);
    TEST_ASSERT_FALSE(result);

    nxp_common_request_processing_Expect();
    result = nexus_channel_link_manager_create_link(
        &linked_id,
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    TEST_ASSERT_TRUE(result);

    result =
        _nexus_channel_link_manager_link_from_nxid(&linked_id, &result_link);
    // need to call `process` to create the link
    TEST_ASSERT_FALSE(result);

    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_CONTROLLER);
    nexus_channel_link_manager_process(0);
    result =
        _nexus_channel_link_manager_link_from_nxid(&linked_id, &result_link);
    TEST_ASSERT_TRUE(result);

    TEST_ASSERT_EQUAL_MEMORY(
        &linked_id, &result_link.linked_device_id, sizeof(struct nx_id));
    TEST_ASSERT_EQUAL_MEMORY(&link_key,
                             &result_link.security_data.mode0.sym_key,
                             sizeof(struct nx_common_check_key));
    TEST_ASSERT_EQUAL(sec_data.mode0.nonce,
                      result_link.security_data.mode0.nonce);
    TEST_ASSERT_EQUAL(CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
                      result_link.operating_mode);
    TEST_ASSERT_EQUAL(
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        result_link.security_mode);

    nexus_channel_link_manager_process(100);

    TEST_ASSERT_EQUAL(
        100, _nexus_channel_link_manager_secs_since_link_active(&linked_id));

    nexus_channel_link_manager_reset_link_secs_since_active(&linked_id);

    TEST_ASSERT_EQUAL(
        0, _nexus_channel_link_manager_secs_since_link_active(&linked_id));
}

void test_link_manager_reset_link_expires__reset_ok(void)
{
    // initializes with no links present
    struct nx_id linked_id = {5921, 123456};

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    nexus_channel_link_t result_link = {0};

    TEST_ASSERT_EQUAL(
        UINT32_MAX,
        _nexus_channel_link_manager_secs_since_link_active(&linked_id));

    // first, link doesn't exist
    bool result =
        _nexus_channel_link_manager_link_from_nxid(&linked_id, &result_link);
    TEST_ASSERT_FALSE(result);

    nxp_common_request_processing_Expect();
    result = nexus_channel_link_manager_create_link(
        &linked_id,
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    TEST_ASSERT_TRUE(result);

    result =
        _nexus_channel_link_manager_link_from_nxid(&linked_id, &result_link);
    // need to call `process` to create the link
    TEST_ASSERT_FALSE(result);

    nxp_channel_notify_event_Expect(
        NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_CONTROLLER);
    nexus_channel_link_manager_process(0);
    result =
        _nexus_channel_link_manager_link_from_nxid(&linked_id, &result_link);
    TEST_ASSERT_TRUE(result);

    TEST_ASSERT_EQUAL_MEMORY(
        &linked_id, &result_link.linked_device_id, sizeof(struct nx_id));
    TEST_ASSERT_EQUAL_MEMORY(&link_key,
                             &result_link.security_data.mode0.sym_key,
                             sizeof(struct nx_common_check_key));
    TEST_ASSERT_EQUAL(sec_data.mode0.nonce,
                      result_link.security_data.mode0.nonce);
    TEST_ASSERT_EQUAL(CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
                      result_link.operating_mode);
    TEST_ASSERT_EQUAL(
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        result_link.security_mode);

    // a second longer than expiration

    nxp_channel_notify_event_Expect(NXP_CHANNEL_EVENT_LINK_DELETED);
    nexus_channel_link_manager_process(NEXUS_CHANNEL_LINK_TIMEOUT_SECONDS + 1);

    result =
        _nexus_channel_link_manager_link_from_nxid(&linked_id, &result_link);
    TEST_ASSERT_FALSE(result);
}

void test_link_manager_operating_mode__dual_mode_no_links__returns_idle_dual_mode(
    void)
{
    // tests are compiled with 'dual mode', so we handle this case
    enum nexus_channel_link_operating_mode result =
        nexus_channel_link_manager_operating_mode();

    TEST_ASSERT_EQUAL(CHANNEL_LINK_OPERATING_MODE_DUAL_MODE_IDLE, result);
}

void test_link_manager_operating_mode__single_controller_link__controller_mode(
    void)
{
    // tests are compiled with 'dual mode', so we handle this case
    enum nexus_channel_link_operating_mode result;
    struct nx_id linked_acc_id = {5921, 123456};

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

    result = nexus_channel_link_manager_operating_mode();
    TEST_ASSERT_EQUAL(CHANNEL_LINK_OPERATING_MODE_CONTROLLER, result);
}

void test_link_manager_operating_mode__single_accessory_link__accessory_mode(
    void)
{
    enum nexus_channel_link_operating_mode result;
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

    result = nexus_channel_link_manager_operating_mode();
    TEST_ASSERT_EQUAL(CHANNEL_LINK_OPERATING_MODE_ACCESSORY, result);
}

void test_link_manager_operating_mode__controller_and_accessory_link__dual_mode_active(
    void)
{
    enum nexus_channel_link_operating_mode result;
    struct nx_id linked_cont_id = {5921, 123458};
    struct nx_id linked_acc_id = {5921, 123466};

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

    result = nexus_channel_link_manager_operating_mode();
    TEST_ASSERT_EQUAL(CHANNEL_LINK_OPERATING_MODE_DUAL_MODE_ACTIVE, result);
}

void test_link_manager_has_linked_controller__no_controller__returns_false(void)
{
    TEST_ASSERT_FALSE(nexus_channel_link_manager_has_linked_controller());
}

void test_link_manager_has_linked_controller__controller_present__returns_true(
    void)
{
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

    TEST_ASSERT_TRUE(nexus_channel_link_manager_has_linked_controller());
}
