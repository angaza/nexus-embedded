#include "include/nx_common.h"
#include "messaging/coap/coap.h"
#include "messaging/coap/engine.h"
#include "messaging/coap/transactions.h"
#include "oc/api/oc_main.h"
#include "oc/include/nexus_channel_security.h" // for shared security functions
#include "oc/include/oc_api.h"
#include "oc/include/oc_buffer.h"
#include "oc/include/oc_core_res.h"
#include "oc/include/oc_endpoint.h"
#include "oc/include/oc_helpers.h"
#include "oc/include/oc_network_events.h"
#include "oc/include/oc_rep.h"
#include "oc/include/oc_ri.h"
#include "oc/util/oc_etimer.h"
#include "oc/util/oc_memb.h"
#include "oc/util/oc_mmem.h"
#include "oc/util/oc_process.h"
#include "oc/util/oc_timer.h"
#include "src/nexus_channel_core.h"
#include "src/nexus_channel_res_link_hs.h"
#include "src/nexus_channel_res_lm.h"
#include "src/nexus_channel_sm.h"
#include "src/nexus_common_internal.h"
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
#include <mock_nexus_channel_om.h>
#include <mock_nexus_channel_res_payg_credit.h>
#include <mock_nexus_keycode_core.h>
#include <mock_nxp_channel.h>
#include <mock_nxp_common.h>
#include <mock_nxp_keycode.h>
#include <stdbool.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpointer-sign"

/********************************************************
 * DEFINITIONS
 *******************************************************/

// added for Nexus testing, in `oc_mmem.c`
extern void oc_nexus_testing_reinit_mmem_lists(void);
/********************************************************
 * PRIVATE TYPES
 *******************************************************/

/********************************************************
 * PRIVATE DATA
 *******************************************************/
static const oc_interface_mask_t if_mask_arr[] = {OC_IF_BASELINE, OC_IF_RW};

// represents Nexus ID = {53932, 4244308258}, which is stored as 0xACD22201FBFC
// on a LE platform
oc_endpoint_t FAKE_ACCESSORY_ENDPOINT = {
    NULL, // 'next'
    0, // device
    IPV6, // flags
    {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}}, // di
    {(oc_ipv6_addr_t){
        5683, // port
        {// arbitrary link local address that represents a Nexus ID
         0xFE,
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
        2 // scope
    }},
    {{0}}, // addr_local (unused)
    0, // interface index (not used)
    0, // priority (not used)
    0, // ocf_version_t (unused)
};

// translate integer type codes into bytestrings of length 1...
uint8_t COAP_PUT_BYTESTRING[1] = {COAP_PUT};
uint8_t COAP_GET_BYTESTRING[1] = {COAP_GET};
uint8_t COAP_CONTENT_2_05_BYTESTRING[1] = {CONTENT_2_05};

/********************************************************
 * PRIVATE FUNCTIONS
 *******************************************************/
// pull in source file from IoTivity without changing its name
// https://github.com/ThrowTheSwitch/Ceedling/issues/113
TEST_FILE("oc/api/oc_server_api.c")
TEST_FILE("oc/api/oc_client_api.c")
TEST_FILE("oc/deps/tinycbor/cborencoder.c")
TEST_FILE("oc/deps/tinycbor/cborparser.c")

// will be freed in test teardown regardless of test failures
static oc_rep_t* G_OC_REP;
static oc_message_t* G_OC_MESSAGE = 0;

// Setup (called before any 'test_*' function is called, automatically)
void setUp(void)
{
    // We may tangentially trigger events in security manager tests, ignore
    nxp_channel_notify_event_Ignore();
    nxp_common_nv_read_IgnoreAndReturn(true);
    nxp_common_nv_write_IgnoreAndReturn(true);
    nxp_channel_random_value_IgnoreAndReturn(123456);
    nexus_channel_om_init_Ignore();

    nexus_channel_core_init();

    nexus_channel_link_manager_init();

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

    G_OC_REP = 0;
    G_OC_MESSAGE = oc_allocate_message();
    // expect incoming messages to have IPV6 type
    G_OC_MESSAGE->endpoint.flags = IPV6;
}

// Teardown (called after any 'test_*' function is called, automatically)
void tearDown(void)
{
    oc_message_unref(G_OC_MESSAGE);
    if (G_OC_REP != 0)
    {
        oc_free_rep(G_OC_REP);
    }

    nexus_channel_core_shutdown();

    // In some tests, we may leave certain lists with dangling or invalid
    // states if a test fails before IoTivity cleans up. We want to fully
    // erase the IoTivity memory, including linked lists, before moving to
    // the next test.
    oc_nexus_testing_reinit_mmem_lists();
}

void test_nexus_channel_sm__secured_method_list_full__fails(void)
{
    oc_resource_t* res = oc_ri_get_app_resource_by_uri(
        "/nx/pc", strlen("/nx/pc"), NEXUS_CHANNEL_NEXUS_DEVICE_ID);

    // exhaust the possible resource handler allocations
    // WARNING: we assume that the max number of methods allowed is
    // OC_MAX_APP_RESOURCES * 2 as defined in the OC_MEMB initialization in
    // nexus_channel_sm.c
    for (int i = 0; i < OC_MAX_APP_RESOURCES * 2; i++)
    {
        TEST_ASSERT_TRUE(
            nexus_channel_sm_nexus_resource_method_new(res, OC_POST) != NULL);
    }

    TEST_ASSERT_TRUE(nexus_channel_sm_nexus_resource_method_new(res, OC_POST) ==
                     NULL);
}

void test_nexus_channel_sm__register_delete_secured_resource__ok(void)
{
    oc_resource_t* res = oc_ri_get_app_resource_by_uri(
        "/nx/pc", strlen("/nx/pc"), NEXUS_CHANNEL_NEXUS_DEVICE_ID);
    TEST_ASSERT_EQUAL(0, _nexus_channel_sm_secured_resource_methods_count());
    TEST_ASSERT_FALSE(nexus_channel_sm_resource_method_is_secured(res, OC_GET));

    // register secured resource method
    nexus_channel_sm_nexus_resource_method_new(res, OC_PUT);
    TEST_ASSERT_EQUAL(1, _nexus_channel_sm_secured_resource_methods_count());
    TEST_ASSERT_TRUE(nexus_channel_sm_resource_method_is_secured(res, OC_PUT));
    TEST_ASSERT_FALSE(nexus_channel_sm_resource_method_is_secured(res, OC_GET));

    // resource method not registered with security manager; should return false
    TEST_ASSERT_FALSE(
        nexus_channel_sm_resource_method_is_secured(res, OC_POST));

    // resource does not exist; should return false
    TEST_ASSERT_FALSE(
        nexus_channel_sm_resource_method_is_secured(NULL, OC_GET));

    // resource method does not exist; should return false
    nexus_channel_sm_free_all_nexus_resource_methods();
    TEST_ASSERT_EQUAL(0, _nexus_channel_sm_secured_resource_methods_count());
    TEST_ASSERT_FALSE(nexus_channel_sm_resource_method_is_secured(res, OC_PUT));
}

void test_sm_message_headers_secured_mode0__unrecognized_content_format__unsecured_ok(
    void)
{
    coap_packet_t request_packet;
    // initialize packet: PUT with arbitrary message ID
    coap_udp_init_message(&request_packet, COAP_TYPE_CON, 3, 123);
    // set the request URI path and content format
    coap_set_header_uri_path(&request_packet, "/nx/pc", strlen("/nx/pc"));

    // no content_format set; will be classified as unsecured
    // coap_set_header_content_format(&request_packet, APPLICATION_COSE_MAC0);
    TEST_ASSERT_FALSE(
        _nexus_channel_sm_message_headers_secured_mode0(&request_packet));
}

void test_sm_message_headers_secured_mode0__secured_message__secured_ok(void)
{
    coap_packet_t request_packet;
    // initialize packet: PUT with arbitrary message ID
    coap_udp_init_message(&request_packet, COAP_TYPE_CON, 3, 123);
    // set the request URI path and content format
    coap_set_header_uri_path(&request_packet, "/nx/pc", strlen("/nx/pc"));
    coap_set_header_content_format(&request_packet, APPLICATION_COSE_MAC0);

    TEST_ASSERT_TRUE(
        _nexus_channel_sm_message_headers_secured_mode0(&request_packet));
}

void test_sm_message_headers_secured_mode0__unsecured_message__unsecured_ok(
    void)
{
    coap_packet_t request_packet;
    // initialize packet: PUT with arbitrary message ID
    coap_udp_init_message(&request_packet, COAP_TYPE_CON, 3, 123);
    // set the request URI path and content format
    coap_set_header_uri_path(&request_packet, "/nx/pc", strlen("/nx/pc"));
    coap_set_header_content_format(&request_packet, APPLICATION_VND_OCF_CBOR);

    TEST_ASSERT_FALSE(
        _nexus_channel_sm_message_headers_secured_mode0(&request_packet));
}

void test_sm_parse_cose_mac0__empty_inputs_fails(void)
{
    nexus_security_mode0_cose_mac0_t cose_mac0_parsed = {0};

    TEST_ASSERT_FALSE(
        _nexus_channel_sm_parse_cose_mac0(NULL, &cose_mac0_parsed));
}

void test_sm_parse_cose_mac0__format_ok(void)
{
    // payload
    const char* payload = "hello world";
    // MAC
    const uint8_t mac[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22};

    // encode the outgoing data to test
    uint8_t enc_data[OC_BLOCK_SIZE] = {0};
    oc_rep_new(enc_data, OC_BLOCK_SIZE);

    oc_rep_begin_root_object();
    // 'protected' in a bstr; single byte for CoAP type code
    oc_rep_set_byte_string(root, p, COAP_CONTENT_2_05_BYTESTRING, 1);
    // 'unprotected' elements as a map of length 2
    oc_rep_open_object(root, u);
    oc_rep_set_uint(u, 4, 15); // key id 15
    oc_rep_set_uint(u, 5, 0x01020304); // nonce 0x01020304
    oc_rep_close_object(root, u);
    // 'payload' in a bstr
    oc_rep_set_byte_string(root, d, payload, strlen(payload));
    // 'tag' in a bstr
    oc_rep_set_byte_string(root, m, mac, sizeof(mac));
    oc_rep_end_root_object();

    int enc_size = oc_rep_get_encoded_payload_size();

    // lifted from oc_ri.c to initialize oc_rep memory
    char rep_objects_alloc[OC_MAX_NUM_REP_OBJECTS];
    oc_rep_t rep_objects_pool[OC_MAX_NUM_REP_OBJECTS];
    memset(rep_objects_alloc, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(char));
    memset(rep_objects_pool, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(oc_rep_t));
    struct oc_memb rep_objects = {sizeof(oc_rep_t),
                                  OC_MAX_NUM_REP_OBJECTS,
                                  rep_objects_alloc,
                                  (void*) rep_objects_pool,
                                  0};
    oc_rep_set_pool(&rep_objects);

    // decode rep
    nexus_security_mode0_cose_mac0_t cose_mac0_parsed = {0};
    int err = oc_parse_rep(enc_data, enc_size, &G_OC_REP);
    TEST_ASSERT_TRUE(err == 0);
    TEST_ASSERT_TRUE(G_OC_REP != NULL);
    TEST_ASSERT_TRUE(
        _nexus_channel_sm_parse_cose_mac0(G_OC_REP, &cose_mac0_parsed));

    TEST_ASSERT_EQUAL(15, cose_mac0_parsed.kid);
    TEST_ASSERT_EQUAL(0x01020304, cose_mac0_parsed.nonce);
    TEST_ASSERT_EQUAL(strlen(payload), cose_mac0_parsed.payload_len);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        payload, cose_mac0_parsed.payload, strlen(payload));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(mac, cose_mac0_parsed.mac, sizeof(mac));
}

void test_sm_parse_cose_mac0__bad_first_type__fails(void)
{
    // payload
    const char* payload = "hello world";
    // MAC
    const uint8_t mac[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22};

    // encode the outgoing data to test
    uint8_t enc_data[OC_BLOCK_SIZE] = {0};
    oc_rep_new(enc_data, OC_BLOCK_SIZE);

    oc_rep_begin_root_object();
    // FAILURE CAUSE: we expect this to be a bstr
    oc_rep_set_uint(root, p, 0);
    // 'unprotected' elements as a map of length 2
    oc_rep_open_object(root, u);
    oc_rep_set_uint(u, 4, 15); // key id 15
    oc_rep_set_uint(u, 5, 0x01020304); // nonce 0x01020304
    oc_rep_close_object(root, u);
    // 'payload' in a bstr
    oc_rep_set_byte_string(root, d, payload, strlen(payload));
    // 'tag' in a bstr
    oc_rep_set_byte_string(root, m, mac, sizeof(mac));
    oc_rep_end_root_object();

    int enc_size = oc_rep_get_encoded_payload_size();

    // lifted from oc_ri.c to initialize oc_rep memory
    char rep_objects_alloc[OC_MAX_NUM_REP_OBJECTS];
    oc_rep_t rep_objects_pool[OC_MAX_NUM_REP_OBJECTS];
    memset(rep_objects_alloc, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(char));
    memset(rep_objects_pool, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(oc_rep_t));
    struct oc_memb rep_objects = {sizeof(oc_rep_t),
                                  OC_MAX_NUM_REP_OBJECTS,
                                  rep_objects_alloc,
                                  (void*) rep_objects_pool,
                                  0};
    oc_rep_set_pool(&rep_objects);

    // decode rep
    nexus_security_mode0_cose_mac0_t cose_mac0_parsed = {0};
    int err = oc_parse_rep(enc_data, enc_size, &G_OC_REP);
    TEST_ASSERT_TRUE(err == 0);
    TEST_ASSERT_TRUE(G_OC_REP != NULL);
    TEST_ASSERT_FALSE(
        _nexus_channel_sm_parse_cose_mac0(G_OC_REP, &cose_mac0_parsed));
}

void test_sm_parse_cose_mac0_too_many_types__fails(void)
{
    // payload
    const char* payload = "hello world";
    // MAC
    const uint8_t mac[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22};

    // encode the outgoing data to test
    uint8_t enc_data[OC_BLOCK_SIZE] = {0};
    oc_rep_new(enc_data, OC_BLOCK_SIZE);

    oc_rep_begin_root_object();
    // 'protected' in a bstr;
    oc_rep_set_byte_string(root, p, COAP_GET_BYTESTRING, 1);
    // 'unprotected' elements as a map of length 2
    oc_rep_open_object(root, u);
    oc_rep_set_uint(u, 4, 15); // key id 15
    oc_rep_set_uint(u, 5, 0x01020304); // nonce 0x01020304
    oc_rep_close_object(root, u);
    // 'payload' in a bstr
    oc_rep_set_byte_string(root, d, payload, strlen(payload));
    // 'tag' in a bstr
    oc_rep_set_byte_string(root, m, mac, sizeof(mac));
    // FAILURE CAUSE: we expect to only have 4 elements
    oc_rep_set_uint(root, e, 0);
    oc_rep_end_root_object();

    int enc_size = oc_rep_get_encoded_payload_size();

    // lifted from oc_ri.c to initialize oc_rep memory
    char rep_objects_alloc[OC_MAX_NUM_REP_OBJECTS];
    oc_rep_t rep_objects_pool[OC_MAX_NUM_REP_OBJECTS];
    memset(rep_objects_alloc, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(char));
    memset(rep_objects_pool, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(oc_rep_t));
    struct oc_memb rep_objects = {sizeof(oc_rep_t),
                                  OC_MAX_NUM_REP_OBJECTS,
                                  rep_objects_alloc,
                                  (void*) rep_objects_pool,
                                  0};
    oc_rep_set_pool(&rep_objects);

    // decode rep
    nexus_security_mode0_cose_mac0_t cose_mac0_parsed = {0};
    int err = oc_parse_rep(enc_data, enc_size, &G_OC_REP);
    TEST_ASSERT_TRUE(err == 0);
    TEST_ASSERT_TRUE(G_OC_REP != NULL);
    TEST_ASSERT_FALSE(
        _nexus_channel_sm_parse_cose_mac0(G_OC_REP, &cose_mac0_parsed));
}

void test_sm_parse_cose_mac0_mac_wrong_length__fails(void)
{
    // payload
    const char* payload = "hello world";
    // FAILURE CAUSE: MAC should be 8 bytes in length
    const uint8_t mac[7] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11};

    // encode the outgoing data to test
    uint8_t enc_data[OC_BLOCK_SIZE] = {0};
    oc_rep_new(enc_data, OC_BLOCK_SIZE);

    oc_rep_begin_root_object();
    // 'protected' in a bstr;
    oc_rep_set_byte_string(root, p, COAP_GET_BYTESTRING, 1);
    // 'unprotected' elements as a map of length 2
    oc_rep_open_object(root, u);
    oc_rep_set_uint(u, 4, 15); // key id 15
    oc_rep_set_uint(u, 5, 0x01020304); // nonce 0x01020304
    oc_rep_close_object(root, u);
    // 'payload' in a bstr
    oc_rep_set_byte_string(root, d, payload, strlen(payload));
    // 'tag' in a bstr
    oc_rep_set_byte_string(root, m, mac, sizeof(mac));
    oc_rep_end_root_object();

    int enc_size = oc_rep_get_encoded_payload_size();

    // lifted from oc_ri.c to initialize oc_rep memory
    char rep_objects_alloc[OC_MAX_NUM_REP_OBJECTS];
    oc_rep_t rep_objects_pool[OC_MAX_NUM_REP_OBJECTS];
    memset(rep_objects_alloc, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(char));
    memset(rep_objects_pool, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(oc_rep_t));
    struct oc_memb rep_objects = {sizeof(oc_rep_t),
                                  OC_MAX_NUM_REP_OBJECTS,
                                  rep_objects_alloc,
                                  (void*) rep_objects_pool,
                                  0};
    oc_rep_set_pool(&rep_objects);

    // decode rep
    nexus_security_mode0_cose_mac0_t cose_mac0_parsed = {0};
    int err = oc_parse_rep(enc_data, enc_size, &G_OC_REP);
    TEST_ASSERT_TRUE(err == 0);
    TEST_ASSERT_TRUE(G_OC_REP != NULL);
    TEST_ASSERT_FALSE(
        _nexus_channel_sm_parse_cose_mac0(G_OC_REP, &cose_mac0_parsed));
}

void test_sm_repack_no_cose_mac_0__repack_success__ok(void)
{
    coap_packet_t request_packet;
    // initialize packet: PUT with arbitrary message ID
    coap_udp_init_message(&request_packet, COAP_TYPE_CON, 3, 123);
    // set the request URI path and content format
    coap_set_header_uri_path(&request_packet, "/nx/pc", strlen("/nx/pc"));
    coap_set_header_content_format(&request_packet, APPLICATION_COSE_MAC0);

    // payload
    char* payload = "hello world";
    // MAC
    struct nexus_check_value mac = {
        {0x59, 0xBA, 0xC0, 0x74, 0x69, 0xEA, 0xEB, 0x30}};

    // struct used to repack the CoAP packet
    nexus_security_mode0_cose_mac0_t cose_mac0 = {0};
    cose_mac0.payload = payload;
    cose_mac0.payload_len = (uint8_t) strlen(payload);
    cose_mac0.mac = &mac;

    // encode the outgoing data to test
    uint8_t enc_data[OC_BLOCK_SIZE] = {0};
    oc_rep_new(enc_data, OC_BLOCK_SIZE);

    oc_rep_begin_root_object();
    // 'protected' in a bstr;
    oc_rep_set_byte_string(root, p, COAP_PUT_BYTESTRING, 1);
    // 'unprotected' elements as a map of length 2
    oc_rep_open_object(root, u);
    oc_rep_set_uint(u, 4, 15); // key id 15
    oc_rep_set_uint(u, 5, 0x01020304); // nonce 0x01020304
    oc_rep_close_object(root, u);
    // 'payload' in a bstr
    oc_rep_set_byte_string(root, d, payload, strlen(payload));
    // 'tag' in a bstr
    oc_rep_set_byte_string(root, m, mac.bytes, sizeof(mac.bytes));
    // sanity check..
    TEST_ASSERT_EQUAL(sizeof(mac), sizeof(mac.bytes));
    TEST_ASSERT_EQUAL(8, sizeof(mac));
    oc_rep_end_root_object();

    int enc_size = oc_rep_get_encoded_payload_size();

    // lifted from oc_ri.c to initialize oc_rep memory
    char rep_objects_alloc[OC_MAX_NUM_REP_OBJECTS];
    oc_rep_t rep_objects_pool[OC_MAX_NUM_REP_OBJECTS];
    memset(rep_objects_alloc, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(char));
    memset(rep_objects_pool, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(oc_rep_t));
    struct oc_memb rep_objects = {sizeof(oc_rep_t),
                                  OC_MAX_NUM_REP_OBJECTS,
                                  rep_objects_alloc,
                                  (void*) rep_objects_pool,
                                  0};
    oc_rep_set_pool(&rep_objects);

    coap_set_payload(&request_packet, enc_data, enc_size);
    coap_set_header_content_format(&request_packet, APPLICATION_COSE_MAC0);

    // includes security information
    TEST_ASSERT_EQUAL(request_packet.payload_len, enc_size);
    int cf = 0;
    coap_get_header_content_format(&request_packet, &cf);
    TEST_ASSERT_EQUAL(APPLICATION_COSE_MAC0, cf);

    _nexus_channel_sm_repack_no_cose_mac0(&request_packet, &cose_mac0);

    // a bit brittle; this is the size of the CBOR-encoded map representing
    // a single key-bytestring pair ({"d": "hello world"})
    TEST_ASSERT_EQUAL(request_packet.payload_len, 16);
    coap_get_header_content_format(&request_packet, &cf);
    TEST_ASSERT_EQUAL(APPLICATION_VND_OCF_CBOR, cf);
}

void test_nexus_channel_authenticate_message__method_secured_message_secured__ok(
    void)
{
    // initializes with no links present
    struct nx_id linked_id = {0};
    linked_id.authority_id = 53932;
    linked_id.device_id = 4244308258;

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data;
    memset(&sec_data, 0xBB, sizeof(sec_data)); // arbitrary

    sec_data.mode0.nonce = 0;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    // create a link
    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_create_link(
        &linked_id,
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nexus_channel_link_manager_process(0);

    // initialize resources
    oc_resource_t* res = oc_ri_get_app_resource_by_uri(
        "/nx/pc", strlen("/nx/pc"), NEXUS_CHANNEL_NEXUS_DEVICE_ID);

    // register a new secured resource handler
    nexus_channel_sm_nexus_resource_method_new(res, OC_PUT);

    coap_packet_t request_packet;
    // initialize packet: PUT with arbitrary message ID
    coap_udp_init_message(&request_packet, COAP_TYPE_CON, 3, 123);
    // set the request URI path and content format
    coap_set_header_uri_path(&request_packet, "/nx/pc", strlen("/nx/pc"));
    coap_set_header_content_format(&request_packet, APPLICATION_COSE_MAC0);

    // payload
    const char* payload = "hello world";
    // MAC
    const uint8_t mac[8] = {0xD1, 0x95, 0xAA, 0x31, 0xA3, 0xC5, 0xEC, 0xEC};

    // encode the outgoing data to test
    uint8_t enc_data[OC_BLOCK_SIZE] = {0};
    oc_rep_new(enc_data, OC_BLOCK_SIZE);

    oc_rep_begin_root_object();
    // 'protected' in a bstr;
    oc_rep_set_byte_string(root, p, COAP_PUT_BYTESTRING, 1);
    // 'unprotected' elements as a map of length 2
    oc_rep_open_object(root, u);
    oc_rep_set_uint(u, 4, 15); // key id 15
    oc_rep_set_uint(u, 5, 0x00000005); // nonce 5
    oc_rep_close_object(root, u);
    // 'payload' in a bstr
    oc_rep_set_byte_string(root, d, payload, strlen(payload));
    // 'tag' in a bstr
    oc_rep_set_byte_string(root, m, mac, sizeof(mac));
    oc_rep_end_root_object();

    int enc_size = oc_rep_get_encoded_payload_size();

    // lifted from oc_ri.c to initialize oc_rep memory
    char rep_objects_alloc[OC_MAX_NUM_REP_OBJECTS];
    oc_rep_t rep_objects_pool[OC_MAX_NUM_REP_OBJECTS];
    memset(rep_objects_alloc, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(char));
    memset(rep_objects_pool, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(oc_rep_t));
    struct oc_memb rep_objects = {sizeof(oc_rep_t),
                                  OC_MAX_NUM_REP_OBJECTS,
                                  rep_objects_alloc,
                                  (void*) rep_objects_pool,
                                  0};
    oc_rep_set_pool(&rep_objects);

    coap_set_payload(&request_packet, enc_data, enc_size);
    // includes security information
    TEST_ASSERT_EQUAL(request_packet.payload_len, enc_size);

    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);
    TEST_ASSERT_EQUAL(0, sec_data.mode0.nonce);
    coap_status_t status = nexus_channel_authenticate_message(
        &FAKE_ACCESSORY_ENDPOINT, &request_packet);
    TEST_ASSERT_EQUAL(COAP_NO_ERROR, status);
    // security information stripped out
    TEST_ASSERT_TRUE((int) request_packet.payload_len < enc_size);
    // a bit brittle; this is the size of the CBOR-encoded map representing
    // a single key-bytestring pair ({"d": "hello world"})
    TEST_ASSERT_EQUAL(request_packet.payload_len, 16);
    // should have incremented the nonce
    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);
    TEST_ASSERT_EQUAL(5, sec_data.mode0.nonce);
}

void test_nexus_channel_authenticate_message__method_unsecured_message_secured__ok(
    void)
{
    // initializes with no links present
    struct nx_id linked_id = {0};
    linked_id.authority_id = 53932;
    linked_id.device_id = 4244308258;

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data = {
        0}; // nonce and sym key are set explicitly below

    sec_data.mode0.nonce = 0;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    // create a link
    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_create_link(
        &linked_id,
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nexus_channel_link_manager_process(0);

    coap_packet_t request_packet;
    // initialize packet: GET with arbitrary message ID
    coap_udp_init_message(&request_packet, COAP_TYPE_CON, 1, 123);
    // set the request URI path and content format
    coap_set_header_uri_path(&request_packet, "/nx/pc", strlen("/nx/pc"));
    coap_set_header_content_format(&request_packet, APPLICATION_COSE_MAC0);

    // payload
    const char* payload = "hello world";
    // MAC
    const uint8_t mac[8] = {0xE6, 0x3F, 0x7E, 0xA8, 0xD8, 0x05, 0x88, 0x2B};

    // encode the outgoing data to test
    uint8_t enc_data[OC_BLOCK_SIZE] = {0};
    oc_rep_new(enc_data, OC_BLOCK_SIZE);

    oc_rep_begin_root_object();
    // 'protected' in a bstr; empty for now
    oc_rep_set_byte_string(root, p, COAP_GET_BYTESTRING, 1);
    // 'unprotected' elements as a map of length 2
    oc_rep_open_object(root, u);
    oc_rep_set_uint(u, 4, 15); // key id 15
    oc_rep_set_uint(u, 5, 0x01020304); // nonce 0x01020304
    oc_rep_close_object(root, u);
    // 'payload' in a bstr
    oc_rep_set_byte_string(root, d, payload, strlen(payload));
    // 'tag' in a bstr
    oc_rep_set_byte_string(root, m, mac, sizeof(mac));
    oc_rep_end_root_object();

    int enc_size = oc_rep_get_encoded_payload_size();

    // lifted from oc_ri.c to initialize oc_rep memory
    char rep_objects_alloc[OC_MAX_NUM_REP_OBJECTS];
    oc_rep_t rep_objects_pool[OC_MAX_NUM_REP_OBJECTS];
    memset(rep_objects_alloc, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(char));
    memset(rep_objects_pool, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(oc_rep_t));
    struct oc_memb rep_objects = {sizeof(oc_rep_t),
                                  OC_MAX_NUM_REP_OBJECTS,
                                  rep_objects_alloc,
                                  (void*) rep_objects_pool,
                                  0};
    oc_rep_set_pool(&rep_objects);

    coap_set_payload(&request_packet, enc_data, enc_size);
    // includes security information
    TEST_ASSERT_EQUAL(request_packet.payload_len, enc_size);

    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);
    TEST_ASSERT_EQUAL(0, sec_data.mode0.nonce);
    coap_status_t status = nexus_channel_authenticate_message(
        &FAKE_ACCESSORY_ENDPOINT, &request_packet);
    TEST_ASSERT_EQUAL(COAP_NO_ERROR, status);
    // security information stripped out
    TEST_ASSERT_TRUE((int) request_packet.payload_len < enc_size);
    // a bit brittle; this is the size of the CBOR-encoded map representing
    // a single key-bytestring pair ({"d": "hello world"})
    TEST_ASSERT_EQUAL(request_packet.payload_len, 16);
    // should have incremented the nonce
    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);
    TEST_ASSERT_EQUAL(0x01020304, sec_data.mode0.nonce);
}

void test_nexus_channel_authenticate_message__resource_secured_message_unsecured__fails(
    void)
{
    // initializes with no links present
    struct nx_id linked_id = {0};
    linked_id.authority_id = 53932;
    linked_id.device_id = 4244308258;

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
    nexus_channel_link_manager_process(0);

    // initialize resources
    oc_resource_t* res = oc_ri_get_app_resource_by_uri(
        "/nx/pc", strlen("/nx/pc"), NEXUS_CHANNEL_NEXUS_DEVICE_ID);

    // register a new secured resource handler
    nexus_channel_sm_nexus_resource_method_new(res, OC_PUT);

    coap_packet_t request_packet;
    // initialize packet: PUT with arbitrary message ID
    coap_udp_init_message(&request_packet, COAP_TYPE_CON, 3, 123);
    // set the request URI path and content format
    coap_set_header_uri_path(&request_packet, "/nx/pc", strlen("/nx/pc"));
    coap_set_header_content_format(&request_packet, APPLICATION_VND_OCF_CBOR);

    // encode the outgoing data to test
    uint8_t enc_data[OC_BLOCK_SIZE] = {0};
    oc_rep_new(enc_data, OC_BLOCK_SIZE);

    oc_rep_begin_root_object();
    oc_rep_set_byte_string(root, p, COAP_PUT_BYTESTRING, 1);
    oc_rep_end_root_object();

    int enc_size = oc_rep_get_encoded_payload_size();

    // lifted from oc_ri.c to initialize oc_rep memory
    char rep_objects_alloc[OC_MAX_NUM_REP_OBJECTS];
    oc_rep_t rep_objects_pool[OC_MAX_NUM_REP_OBJECTS];
    memset(rep_objects_alloc, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(char));
    memset(rep_objects_pool, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(oc_rep_t));
    struct oc_memb rep_objects = {sizeof(oc_rep_t),
                                  OC_MAX_NUM_REP_OBJECTS,
                                  rep_objects_alloc,
                                  (void*) rep_objects_pool,
                                  0};
    oc_rep_set_pool(&rep_objects);

    coap_set_payload(&request_packet, enc_data, enc_size);
    TEST_ASSERT_EQUAL(request_packet.payload_len, enc_size);

    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);
    TEST_ASSERT_EQUAL(5, sec_data.mode0.nonce);
    coap_status_t status = nexus_channel_authenticate_message(
        &FAKE_ACCESSORY_ENDPOINT, &request_packet);
    TEST_ASSERT_EQUAL(UNAUTHORIZED_4_01, status);
    // nonce should be unchanged
    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);
    TEST_ASSERT_EQUAL(5, sec_data.mode0.nonce);
}

void test_nexus_channel_authenticate_message__method_secured_message_secured_cose_mac0_parsing_failure__fails(
    void)
{
    // initializes with no links present
    struct nx_id linked_id = {0};
    linked_id.authority_id = 53932;
    linked_id.device_id = 4244308258;

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
    nexus_channel_link_manager_process(0);

    // initialize resources
    oc_resource_t* res = oc_ri_get_app_resource_by_uri(
        "/nx/pc", strlen("/nx/pc"), NEXUS_CHANNEL_NEXUS_DEVICE_ID);

    // register a new secured resource handler
    nexus_channel_sm_nexus_resource_method_new(res, OC_PUT);

    coap_packet_t request_packet;
    // initialize packet: PUT with arbitrary message ID
    coap_udp_init_message(&request_packet, COAP_TYPE_CON, 3, 123);
    // set the request URI path and content format
    coap_set_header_uri_path(&request_packet, "/nx/pc", strlen("/nx/pc"));
    coap_set_header_content_format(&request_packet, APPLICATION_COSE_MAC0);

    // payload
    const char* payload = "hello world";
    // MAC
    const uint8_t mac[8] = {0xcc, 0xc9, 0x4e, 0xf8, 0x18, 0x22, 0x3b, 0xca};

    // encode the outgoing data to test
    uint8_t enc_data[OC_BLOCK_SIZE] = {0};
    oc_rep_new(enc_data, OC_BLOCK_SIZE);

    // CAUSE OF FAILURE: COSE_MAC0 is improperly packed (no protected element)
    oc_rep_begin_root_object();
    // 'unprotected' elements as a map of length 2
    oc_rep_open_object(root, u);
    oc_rep_set_uint(u, 4, 15); // key id 15
    oc_rep_set_uint(u, 5, 0x01020304); // nonce 0x01020304
    oc_rep_close_object(root, u);
    // 'payload' in a bstr
    oc_rep_set_byte_string(root, d, payload, strlen(payload));
    // 'tag' in a bstr
    oc_rep_set_byte_string(root, m, mac, sizeof(mac));
    oc_rep_end_root_object();

    int enc_size = oc_rep_get_encoded_payload_size();

    // lifted from oc_ri.c to initialize oc_rep memory
    char rep_objects_alloc[OC_MAX_NUM_REP_OBJECTS];
    oc_rep_t rep_objects_pool[OC_MAX_NUM_REP_OBJECTS];
    memset(rep_objects_alloc, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(char));
    memset(rep_objects_pool, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(oc_rep_t));
    struct oc_memb rep_objects = {sizeof(oc_rep_t),
                                  OC_MAX_NUM_REP_OBJECTS,
                                  rep_objects_alloc,
                                  (void*) rep_objects_pool,
                                  0};
    oc_rep_set_pool(&rep_objects);

    coap_set_payload(&request_packet, enc_data, enc_size);
    // includes security information
    TEST_ASSERT_EQUAL(request_packet.payload_len, enc_size);

    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);
    TEST_ASSERT_EQUAL(5, sec_data.mode0.nonce);
    coap_status_t status = nexus_channel_authenticate_message(
        &FAKE_ACCESSORY_ENDPOINT, &request_packet);
    TEST_ASSERT_EQUAL(BAD_REQUEST_4_00, status);
    // nonce should be unchanged
    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);
    TEST_ASSERT_EQUAL(5, sec_data.mode0.nonce);
}

void test_nexus_channel_authenticate_message__method_secured_message_secured_invalid_nonce__fails(
    void)
{
    // initializes with no links present
    struct nx_id linked_id = {0};
    linked_id.authority_id = 53932;
    linked_id.device_id = 4244308258;

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
    nexus_channel_link_manager_process(0);

    // initialize resources
    oc_resource_t* res = oc_ri_get_app_resource_by_uri(
        "/nx/pc", strlen("/nx/pc"), NEXUS_CHANNEL_NEXUS_DEVICE_ID);

    // register a new secured resource handler
    nexus_channel_sm_nexus_resource_method_new(res, OC_PUT);

    coap_packet_t request_packet;
    // initialize packet: PUT with arbitrary message ID
    coap_udp_init_message(&request_packet, COAP_TYPE_CON, 3, 123);
    // set the request URI path and content format
    coap_set_header_uri_path(&request_packet, "/nx/pc", strlen("/nx/pc"));
    coap_set_header_content_format(&request_packet, APPLICATION_COSE_MAC0);

    // payload
    const char* payload = "hello world";
    // MAC
    const uint8_t mac[8] = {0x59, 0xBA, 0xC0, 0x74, 0x69, 0xEA, 0xEB, 0x30};

    // encode the outgoing data to test
    uint8_t enc_data[OC_BLOCK_SIZE] = {0};
    oc_rep_new(enc_data, OC_BLOCK_SIZE);

    oc_rep_begin_root_object();
    // 'protected' in a bstr; single byte for CoAP type code
    oc_rep_set_byte_string(root, p, COAP_PUT_BYTESTRING, 1);
    // 'unprotected' elements as a map of length 2
    oc_rep_open_object(root, u);
    oc_rep_set_uint(u, 4, 15); // key id 15
    oc_rep_set_uint(u, 5, 1); // FAILURE CAUSE: nonce 1, too low
    oc_rep_close_object(root, u);
    // 'payload' in a bstr
    oc_rep_set_byte_string(root, d, payload, strlen(payload));
    // 'tag' in a bstr
    oc_rep_set_byte_string(root, m, mac, sizeof(mac));
    oc_rep_end_root_object();

    int enc_size = oc_rep_get_encoded_payload_size();

    // lifted from oc_ri.c to initialize oc_rep memory
    char rep_objects_alloc[OC_MAX_NUM_REP_OBJECTS];
    oc_rep_t rep_objects_pool[OC_MAX_NUM_REP_OBJECTS];
    memset(rep_objects_alloc, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(char));
    memset(rep_objects_pool, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(oc_rep_t));
    struct oc_memb rep_objects = {sizeof(oc_rep_t),
                                  OC_MAX_NUM_REP_OBJECTS,
                                  rep_objects_alloc,
                                  (void*) rep_objects_pool,
                                  0};
    oc_rep_set_pool(&rep_objects);

    coap_set_payload(&request_packet, enc_data, enc_size);
    // includes security information
    TEST_ASSERT_EQUAL(request_packet.payload_len, enc_size);

    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);
    TEST_ASSERT_EQUAL(5, sec_data.mode0.nonce);
    coap_status_t status = nexus_channel_authenticate_message(
        &FAKE_ACCESSORY_ENDPOINT, &request_packet);
    TEST_ASSERT_EQUAL(NOT_ACCEPTABLE_4_06, status);
    // nonce should be unchanged
    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);
    TEST_ASSERT_EQUAL(5, sec_data.mode0.nonce);
}

void test_nexus_channel_authenticate_message__method_secured_message_secured_no_security_info_for_link__fails(
    void)
{
    // initializes with no links present
    struct nx_id linked_id = {0};
    // CAUSE OF FAILURE: INCOMING MESSAGE NX_ID DOES NOT MATCH LINKED NX_ID
    linked_id.authority_id = 12345;
    linked_id.device_id = 4244308258;

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
    nexus_channel_link_manager_process(0);

    // initialize resources
    oc_resource_t* res = oc_ri_get_app_resource_by_uri(
        "/nx/pc", strlen("/nx/pc"), NEXUS_CHANNEL_NEXUS_DEVICE_ID);

    // register a new secured resource handler
    nexus_channel_sm_nexus_resource_method_new(res, OC_PUT);

    coap_packet_t request_packet;
    // initialize packet: PUT with arbitrary message ID
    coap_udp_init_message(&request_packet, COAP_TYPE_CON, 3, 123);
    // set the request URI path and content format
    coap_set_header_uri_path(&request_packet, "/nx/pc", strlen("/nx/pc"));
    coap_set_header_content_format(&request_packet, APPLICATION_COSE_MAC0);

    // payload
    const char* payload = "hello world";
    // MAC
    const uint8_t mac[8] = {0x59, 0xBA, 0xC0, 0x74, 0x69, 0xEA, 0xEB, 0x30};

    // encode the outgoing data to test
    uint8_t enc_data[OC_BLOCK_SIZE] = {0};
    oc_rep_new(enc_data, OC_BLOCK_SIZE);

    oc_rep_begin_root_object();
    // 'protected' in a bstr; CoAP code
    oc_rep_set_byte_string(root, p, COAP_PUT_BYTESTRING, 1);
    // 'unprotected' elements as a map of length 2
    oc_rep_open_object(root, u);
    oc_rep_set_uint(u, 4, 15); // key id 15
    oc_rep_set_uint(u, 5, 0x01020304); // nonce 0x01020304
    oc_rep_close_object(root, u);
    // 'payload' in a bstr
    oc_rep_set_byte_string(root, d, payload, strlen(payload));
    // 'tag' in a bstr
    oc_rep_set_byte_string(root, m, mac, sizeof(mac));
    oc_rep_end_root_object();

    int enc_size = oc_rep_get_encoded_payload_size();

    // lifted from oc_ri.c to initialize oc_rep memory
    char rep_objects_alloc[OC_MAX_NUM_REP_OBJECTS];
    oc_rep_t rep_objects_pool[OC_MAX_NUM_REP_OBJECTS];
    memset(rep_objects_alloc, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(char));
    memset(rep_objects_pool, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(oc_rep_t));
    struct oc_memb rep_objects = {sizeof(oc_rep_t),
                                  OC_MAX_NUM_REP_OBJECTS,
                                  rep_objects_alloc,
                                  (void*) rep_objects_pool,
                                  0};
    oc_rep_set_pool(&rep_objects);

    coap_set_payload(&request_packet, enc_data, enc_size);
    // includes security information
    TEST_ASSERT_EQUAL(request_packet.payload_len, enc_size);

    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);
    TEST_ASSERT_EQUAL(5, sec_data.mode0.nonce);
    coap_status_t status = nexus_channel_authenticate_message(
        &FAKE_ACCESSORY_ENDPOINT, &request_packet);
    TEST_ASSERT_EQUAL(BAD_REQUEST_4_00, status);
    // nonce should be unchanged
    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);
    TEST_ASSERT_EQUAL(5, sec_data.mode0.nonce);
}

void test_nexus_channel_authenticate_message__method_secured_message_secured__invalid_mac_fails(
    void)
{
    // initializes with no links present
    struct nx_id linked_id = {0};
    linked_id.authority_id = 53932;
    linked_id.device_id = 4244308258;

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
    nexus_channel_link_manager_process(0);

    // initialize resources
    oc_resource_t* res = oc_ri_get_app_resource_by_uri(
        "/nx/pc", strlen("/nx/pc"), NEXUS_CHANNEL_NEXUS_DEVICE_ID);

    // register a new secured resource handler
    nexus_channel_sm_nexus_resource_method_new(res, OC_PUT);

    coap_packet_t request_packet;
    // initialize packet: PUT with arbitrary message ID
    coap_udp_init_message(&request_packet, COAP_TYPE_CON, 3, 123);
    // set the request URI path and content format
    coap_set_header_uri_path(&request_packet, "/nx/pc", strlen("/nx/pc"));
    coap_set_header_content_format(&request_packet, APPLICATION_COSE_MAC0);

    // payload
    const char* payload = "hello world";
    // CAUSE OF FAILURE: MAC in COSE_MAC is incorrect
    const uint8_t mac[8] = {0xFF, 0xBA, 0xC0, 0x74, 0x69, 0xEA, 0xEB, 0x30};

    // encode the outgoing data to test
    uint8_t enc_data[OC_BLOCK_SIZE] = {0};
    oc_rep_new(enc_data, OC_BLOCK_SIZE);

    oc_rep_begin_root_object();
    // 'protected' in a bstr
    oc_rep_set_byte_string(root, p, COAP_PUT_BYTESTRING, 1);
    // 'unprotected' elements as a map of length 2
    oc_rep_open_object(root, u);
    oc_rep_set_uint(u, 4, 15); // key id 15
    oc_rep_set_uint(u, 5, 0x01020304); // nonce 0x01020304
    oc_rep_close_object(root, u);
    // 'payload' in a bstr
    oc_rep_set_byte_string(root, d, payload, strlen(payload));
    // 'tag' in a bstr
    oc_rep_set_byte_string(root, m, mac, sizeof(mac));
    oc_rep_end_root_object();

    int enc_size = oc_rep_get_encoded_payload_size();

    // lifted from oc_ri.c to initialize oc_rep memory
    char rep_objects_alloc[OC_MAX_NUM_REP_OBJECTS];
    oc_rep_t rep_objects_pool[OC_MAX_NUM_REP_OBJECTS];
    memset(rep_objects_alloc, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(char));
    memset(rep_objects_pool, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(oc_rep_t));
    struct oc_memb rep_objects = {sizeof(oc_rep_t),
                                  OC_MAX_NUM_REP_OBJECTS,
                                  rep_objects_alloc,
                                  (void*) rep_objects_pool,
                                  0};
    oc_rep_set_pool(&rep_objects);

    coap_set_payload(&request_packet, enc_data, enc_size);
    // includes security information
    TEST_ASSERT_EQUAL(request_packet.payload_len, enc_size);

    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);
    TEST_ASSERT_EQUAL(5, sec_data.mode0.nonce);
    coap_status_t status = nexus_channel_authenticate_message(
        &FAKE_ACCESSORY_ENDPOINT, &request_packet);
    TEST_ASSERT_EQUAL(UNAUTHORIZED_4_01, status);
    // nonce should be unchanged
    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);
    TEST_ASSERT_EQUAL(5, sec_data.mode0.nonce);
}

void test_nexus_channel_authenticate_message__method_unsecured_message_unsecured__ok(
    void)
{
    coap_packet_t request_packet;
    // initialize packet: GET with arbitrary message ID
    coap_udp_init_message(&request_packet, COAP_TYPE_CON, 1, 123);
    // set the request URI path and content format
    coap_set_header_uri_path(&request_packet, "/nx/pc", strlen("/nx/pc"));
    coap_set_header_content_format(&request_packet, APPLICATION_VND_OCF_CBOR);

    // payload
    const char* payload = "hello world";

    // encode the outgoing data to test
    uint8_t enc_data[OC_BLOCK_SIZE] = {0};
    oc_rep_new(enc_data, OC_BLOCK_SIZE);
    oc_rep_begin_root_object();

    // 'payload' in a bstr
    oc_rep_set_byte_string(root, d, payload, strlen(payload));

    oc_rep_end_root_object();
    int enc_size = oc_rep_get_encoded_payload_size();

    // lifted from oc_ri.c to initialize oc_rep memory
    char rep_objects_alloc[OC_MAX_NUM_REP_OBJECTS];
    oc_rep_t rep_objects_pool[OC_MAX_NUM_REP_OBJECTS];
    memset(rep_objects_alloc, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(char));
    memset(rep_objects_pool, 0, OC_MAX_NUM_REP_OBJECTS * sizeof(oc_rep_t));
    struct oc_memb rep_objects = {sizeof(oc_rep_t),
                                  OC_MAX_NUM_REP_OBJECTS,
                                  rep_objects_alloc,
                                  (void*) rep_objects_pool,
                                  0};
    oc_rep_set_pool(&rep_objects);

    coap_set_payload(&request_packet, enc_data, enc_size);
    TEST_ASSERT_EQUAL(request_packet.payload_len, enc_size);

    coap_status_t status = nexus_channel_authenticate_message(
        &FAKE_ACCESSORY_ENDPOINT, &request_packet);
    TEST_ASSERT_EQUAL(COAP_NO_ERROR, status);
    // no changes to the payload
    TEST_ASSERT_EQUAL(request_packet.payload_len, enc_size);
}

void test_coap_nexus_engine__resource_unsecured_message_unsecured__ok(void)
{
    coap_packet_t request_packet = {0};
    // initialize packet: GET with arbitrary message ID
    coap_udp_init_message(&request_packet, COAP_TYPE_CON, 1, 123);
    // set the request URI path and content format
    coap_set_header_uri_path(&request_packet, "/nx/pc", strlen("/nx/pc"));
    coap_set_header_content_format(&request_packet, APPLICATION_VND_OCF_CBOR);

    if (G_OC_MESSAGE)
    {
        // set the message length based on the result of data serialization
        G_OC_MESSAGE->length =
            coap_serialize_message(&request_packet, G_OC_MESSAGE->data);
    }

    TEST_ASSERT_EQUAL(0, oc_process_nevents());

    oc_network_event(G_OC_MESSAGE);

    // XXX: does not call nxp_channel_network_send because payg_credit_get
    // is currently not implemented!
    nexus_channel_res_payg_credit_get_handler_ExpectAnyArgs();
    nexus_channel_core_process(0);

    TEST_ASSERT_EQUAL(0, oc_process_nevents());
}

void test_coap_nexus_engine__resource_secured_message_unsecured__fails(void)
{
    // initialize resources
    oc_resource_t* res = oc_ri_get_app_resource_by_uri(
        "/nx/pc", strlen("/nx/pc"), NEXUS_CHANNEL_NEXUS_DEVICE_ID);

    // register a new secured resource handler
    // Although there is no PUT registered on the nx/pc resource,
    // security management happens before we pass the message to the
    // 'unsecured' coap handler.
    nexus_channel_sm_nexus_resource_method_new(res, OC_PUT);

    coap_packet_t request_packet = {0};
    // initialize packet: PUT with arbitrary message ID
    coap_udp_init_message(&request_packet, COAP_TYPE_NON, 3, 123);
    // set a 1-byte token per Nexus Channel Core spec
    uint8_t token_val = 0xfa;
    coap_set_token(&request_packet, &token_val, 1);
    // set the request URI path and content format
    coap_set_header_uri_path(&request_packet, "/nx/pc", strlen("/nx/pc"));
    coap_set_header_content_format(&request_packet, APPLICATION_VND_OCF_CBOR);

    if (G_OC_MESSAGE)
    {
        // set the message length based on the result of data serialization
        G_OC_MESSAGE->length =
            coap_serialize_message(&request_packet, G_OC_MESSAGE->data);
    }

    TEST_ASSERT_EQUAL(0, oc_process_nevents());

    oc_network_event(G_OC_MESSAGE);

    // process the message; should not get to `coap_receive` and sends an
    // early error reply because of failed authentication
    nxp_common_request_processing_Expect();

    struct nx_id fake_id = {0, 12345678};
    nxp_channel_get_nexus_id_ExpectAndReturn(fake_id);
    nxp_channel_network_send_ExpectAnyArgsAndReturn(0);

    nexus_channel_core_process(0);

    TEST_ASSERT_EQUAL(0, oc_process_nevents());
}

void test_do_post_cose_mac0_appended__ok(void)
{
    const char* uri = "nx/pc";

    // initialize resources
    oc_resource_t* res = oc_ri_get_app_resource_by_uri(
        uri, strlen(uri), NEXUS_CHANNEL_NEXUS_DEVICE_ID);

    // register a new secured resource handler
    nexus_channel_sm_nexus_resource_method_new(res, OC_POST);

    // initializes with no links present
    struct nx_id linked_id = {0};
    linked_id.authority_id = 53932;
    linked_id.device_id = 4244308258;

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data = {
        0}; // nonce and sym key are set explicitly below

    sec_data.mode0.nonce = 0;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    // create a link
    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_create_link(
        &linked_id,
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nexus_channel_core_process(0);
    TEST_ASSERT_EQUAL(0, oc_process_nevents());

    nxp_channel_get_nexus_id_IgnoreAndReturn(linked_id);
    nxp_channel_network_send_IgnoreAndReturn(NX_CHANNEL_ERROR_NONE);
    TEST_ASSERT_TRUE(
        oc_init_post(uri, &FAKE_ACCESSORY_ENDPOINT, NULL, NULL, LOW_QOS, NULL));
    TEST_ASSERT_EQUAL(0, oc_process_nevents());

    // payload
    const char* payload = "hello world";

    // encode the outgoing data to test
    oc_rep_begin_root_object();
    // 'payload' in a bstr
    oc_rep_set_byte_string(root, d, payload, strlen(payload));
    oc_rep_end_root_object();

    // message ID to look up is the last one used (allocated to callback created
    // in
    // `oc_init_post`)
    uint16_t current_mid = coap_get_mid();
    coap_transaction_t* t = NULL;
    t = coap_get_transaction_by_mid(current_mid - 1);

    TEST_ASSERT_TRUE(t != NULL);

    TEST_ASSERT_TRUE(oc_do_post());

    // one event for outgoing message
    TEST_ASSERT_EQUAL(1, oc_process_nevents());
    nexus_channel_core_process(0);

    /* gdb output at oc_send_buffer:
     * print message->length
     * $ = 75
     *
     * from a previous test we can print the CBOR-packed payload
     * print /x *request_packet.payload@16
     * $ = {0xbf, 0x61, 0x64, 0x4b, 0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x77,
     * 0x6f, 0x72, 0x6c, 0x64, 0xff}
     *
     * in the output below you can see the same bytestring, indicating repacking
     * packing successful
     * print /x *message->data@75
     * $ = {0x58, 0x2, 0xe2, 0x41, 0x40, 0xe2, 0x1, 0x0, 0x40, 0xe2, 0x1, 0x0,
     * 0xb2, 0x6e, 0x78, 0x2, 0x70, 0x63, 0x11, 0x11, 0x52, 0x27, 0x10, 0xe2,
     * 0x6, 0xe3, 0x8, 0x0, 0xff, 0xbf, 0x61, 0x70, 0x41, 0x2, 0x61,
     * 0x75, 0xbf, 0x61, 0x34, 0x0, 0x61, 0x35, 0x0, 0xff, 0x61, 0x64, 0x50,
     * 0xbf, 0x61, 0x64, 0x4b, 0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x77, 0x6f,
     * 0x72, 0x6c, 0x64, 0xff, 0x61, 0x6d, 0x48, 0xc9, 0x96, 0xe8, 0xca,
     * 0x7a, 0x73, 0x29, 0x8, 0xff}
     */
}

#pragma GCC diagnostic pop
