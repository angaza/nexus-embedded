#include "include/nx_common.h"
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
#include "src/nexus_cose_mac0_common.h"
#include "src/nexus_cose_mac0_sign.h"
#include "src/nexus_cose_mac0_verify.h"
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
    nexus_channel_res_payg_credit_process_IgnoreAndReturn(UINT32_MAX);

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

    // create a cose mac0 signed payload and set it as the packet payload

    // HELLO WORLD
    uint8_t payload_to_secure[11] = {
        0x48, 0x45, 0x4C, 0x4C, 0x4F, 0x20, 0x57, 0x4F, 0x52, 0x4C, 0x44};
    const nexus_cose_mac0_common_macparams_t mac_params = {
        &link_key,
        38,
        // aad
        {
            request_packet.code,
            (uint8_t*) request_packet.uri_path,
            request_packet.uri_path_len,
        },
        payload_to_secure,
        sizeof(payload_to_secure),
    };

    uint8_t enc_data[NEXUS_CHANNEL_MAX_CBOR_PAYLOAD_SIZE];
    size_t enc_size;
    nexus_cose_error encode_result = nexus_cose_mac0_sign_encode_message(
        &mac_params, enc_data, sizeof(enc_data), &enc_size);
    TEST_ASSERT_EQUAL(NEXUS_COSE_ERROR_NONE, encode_result);

    coap_set_payload(&request_packet, enc_data, enc_size);
    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);
    TEST_ASSERT_EQUAL(0, sec_data.mode0.nonce);
    nexus_channel_sm_auth_error_t auth_result =
        nexus_channel_authenticate_message(&FAKE_ACCESSORY_ENDPOINT,
                                           &request_packet);
    TEST_ASSERT_EQUAL(NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_NONE, auth_result);
    // security information stripped out
    TEST_ASSERT_TRUE((int) request_packet.payload_len < (int) enc_size);
    // after re-encoding, the length is exactly equal to the original
    // unsecured payload
    TEST_ASSERT_EQUAL(request_packet.payload_len, sizeof(payload_to_secure));
    // should have incremented the nonce to the one received in the message
    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);
    TEST_ASSERT_EQUAL(38, sec_data.mode0.nonce);
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

    // create a cose mac0 signed payload and set it as the packet payload
    // HELLO WORLD
    uint8_t payload_to_secure[11] = {
        0x48, 0x45, 0x4C, 0x4C, 0x4F, 0x20, 0x57, 0x4F, 0x52, 0x4C, 0x44};
    const nexus_cose_mac0_common_macparams_t mac_params = {
        &link_key,
        0x01020304,
        // aad
        {
            request_packet.code,
            (uint8_t*) request_packet.uri_path,
            request_packet.uri_path_len,
        },
        payload_to_secure,
        sizeof(payload_to_secure),
    };

    uint8_t enc_data[NEXUS_CHANNEL_MAX_CBOR_PAYLOAD_SIZE];
    size_t enc_size;
    nexus_cose_error encode_result = nexus_cose_mac0_sign_encode_message(
        &mac_params, enc_data, sizeof(enc_data), &enc_size);
    TEST_ASSERT_EQUAL(NEXUS_COSE_ERROR_NONE, encode_result);

    coap_set_payload(&request_packet, enc_data, enc_size);
    // includes security information
    TEST_ASSERT_EQUAL(request_packet.payload_len, enc_size);

    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);

    TEST_ASSERT_EQUAL(0, sec_data.mode0.nonce);

    uint32_t original_payload_len = request_packet.payload_len;
    nexus_channel_sm_auth_error_t auth_result =
        nexus_channel_authenticate_message(&FAKE_ACCESSORY_ENDPOINT,
                                           &request_packet);
    TEST_ASSERT_EQUAL(NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_NONE, auth_result);
    // security information stripped out
    TEST_ASSERT_TRUE((int) request_packet.payload_len < (int) enc_size);
    // unsecured payload should be smaller than the original secured payload
    TEST_ASSERT_LESS_THAN(original_payload_len, request_packet.payload_len);
    // should have incremented the nonce
    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);
    TEST_ASSERT_EQUAL(0x01020304, sec_data.mode0.nonce);
}

void test_nexus_channel_authenticate_message__payload_to_auth_too_large__return_error(
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

    // create a cose mac0 signed payload and set it as the packet payload,
    // HELLO WORLD
    uint8_t payload_to_secure[11] = {
        0x48, 0x45, 0x4C, 0x4C, 0x4F, 0x20, 0x57, 0x4F, 0x52, 0x4C, 0x44};
    const nexus_cose_mac0_common_macparams_t mac_params = {
        &link_key,
        6,
        // aad
        {
            request_packet.code,
            (uint8_t*) request_packet.uri_path,
            request_packet.uri_path_len,
        },
        payload_to_secure,
        sizeof(payload_to_secure),
    };

    uint8_t enc_data[NEXUS_CHANNEL_MAX_CBOR_PAYLOAD_SIZE];
    size_t enc_size;
    nexus_cose_error encode_result = nexus_cose_mac0_sign_encode_message(
        &mac_params, enc_data, sizeof(enc_data), &enc_size);
    TEST_ASSERT_EQUAL(NEXUS_COSE_ERROR_NONE, encode_result);

    // set the payload with an invalid/too long payload length
    // Cannot use `coap_set_payload` as that function will silently
    // prevent us from exceeding `NEXUS_CHANNEL_MAX_CBOR_PAYLOAD_SIZE`
    request_packet.payload_len = NEXUS_CHANNEL_MAX_CBOR_PAYLOAD_SIZE + 1;
    request_packet.payload = enc_data;
    // fake/invalid payload length is set
    TEST_ASSERT_EQUAL(request_packet.payload_len,
                      NEXUS_CHANNEL_MAX_CBOR_PAYLOAD_SIZE + 1);

    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);
    TEST_ASSERT_EQUAL(5, sec_data.mode0.nonce);

    nexus_channel_sm_auth_error_t auth_result =
        nexus_channel_authenticate_message(&FAKE_ACCESSORY_ENDPOINT,
                                           &request_packet);
    TEST_ASSERT_EQUAL(NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_PAYLOAD_SIZE_INVALID,
                      auth_result);
    // nonce should be unchanged
    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);
    TEST_ASSERT_EQUAL(5, sec_data.mode0.nonce);
}

// a secured message cannot have no payload, by definition - the COSE structure
// requires *some* space
void test_nexus_channel_authenticate_message__payload_to_auth_zero_length__returns_400(
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

    // create a cose mac0 signed payload and set it as the packet payload,
    // HELLO WORLD
    uint8_t payload_to_secure[11] = {
        0x48, 0x45, 0x4C, 0x4C, 0x4F, 0x20, 0x57, 0x4F, 0x52, 0x4C, 0x44};
    const nexus_cose_mac0_common_macparams_t mac_params = {
        &link_key,
        6,
        // aad
        {
            request_packet.code,
            (uint8_t*) request_packet.uri_path,
            request_packet.uri_path_len,
        },
        payload_to_secure,
        sizeof(payload_to_secure),
    };

    uint8_t enc_data[NEXUS_CHANNEL_MAX_CBOR_PAYLOAD_SIZE];
    size_t enc_size;
    nexus_cose_error encode_result = nexus_cose_mac0_sign_encode_message(
        &mac_params, enc_data, sizeof(enc_data), &enc_size);
    TEST_ASSERT_EQUAL(NEXUS_COSE_ERROR_NONE, encode_result);

    // set the payload with an invalid/too long payload length
    coap_set_payload(&request_packet, enc_data, 0);
    // fake/invalid payload length is set
    TEST_ASSERT_EQUAL(request_packet.payload_len, 0);

    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);
    TEST_ASSERT_EQUAL(5, sec_data.mode0.nonce);

    nexus_channel_sm_auth_error_t auth_result =
        nexus_channel_authenticate_message(&FAKE_ACCESSORY_ENDPOINT,
                                           &request_packet);
    TEST_ASSERT_EQUAL(NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_PAYLOAD_SIZE_INVALID,
                      auth_result);
    // nonce should be unchanged
    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);
    TEST_ASSERT_EQUAL(5, sec_data.mode0.nonce);
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

    // create a cose mac0 signed payload and set it as the packet payload,
    // HELLO WORLD
    uint8_t payload_to_secure[11] = {
        0x48, 0x45, 0x4C, 0x4C, 0x4F, 0x20, 0x57, 0x4F, 0x52, 0x4C, 0x44};
    const nexus_cose_mac0_common_macparams_t mac_params = {
        &link_key,
        6,
        // aad
        {
            request_packet.code,
            (uint8_t*) request_packet.uri_path,
            request_packet.uri_path_len,
        },
        payload_to_secure,
        sizeof(payload_to_secure),
    };

    uint8_t enc_data[NEXUS_CHANNEL_MAX_CBOR_PAYLOAD_SIZE];
    size_t enc_size;
    nexus_cose_error encode_result = nexus_cose_mac0_sign_encode_message(
        &mac_params, enc_data, sizeof(enc_data), &enc_size);
    TEST_ASSERT_EQUAL(NEXUS_COSE_ERROR_NONE, encode_result);

    coap_set_payload(&request_packet, enc_data, enc_size);
    TEST_ASSERT_EQUAL(request_packet.payload_len, enc_size);

    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);
    TEST_ASSERT_EQUAL(5, sec_data.mode0.nonce);

    nexus_channel_sm_auth_error_t auth_result =
        nexus_channel_authenticate_message(&FAKE_ACCESSORY_ENDPOINT,
                                           &request_packet);
    TEST_ASSERT_EQUAL(
        NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_RESOURCE_REQUIRES_SECURED_REQUEST,
        auth_result);
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

    uint8_t payload_to_secure[11] = {
        0x48, 0x45, 0x4C, 0x4C, 0x4F, 0x20, 0x57, 0x4F, 0x52, 0x4C, 0x44};
    const nexus_cose_mac0_common_macparams_t mac_params = {
        &link_key,
        6,
        // aad
        {
            request_packet.code,
            (uint8_t*) request_packet.uri_path,
            request_packet.uri_path_len,
        },
        payload_to_secure,
        sizeof(payload_to_secure),
    };

    uint8_t enc_data[NEXUS_CHANNEL_MAX_CBOR_PAYLOAD_SIZE];
    size_t enc_size;
    nexus_cose_error encode_result = nexus_cose_mac0_sign_encode_message(
        &mac_params, enc_data, sizeof(enc_data), &enc_size);
    TEST_ASSERT_EQUAL(NEXUS_COSE_ERROR_NONE, encode_result);

    // offset the payload by 1, so the COSE MAC0 payload is corrupted
    coap_set_payload(&request_packet, enc_data + 1, enc_size - 1);
    TEST_ASSERT_EQUAL(request_packet.payload_len, enc_size - 1);

    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);
    TEST_ASSERT_EQUAL(5, sec_data.mode0.nonce);
    nexus_channel_sm_auth_error_t auth_result =
        nexus_channel_authenticate_message(&FAKE_ACCESSORY_ENDPOINT,
                                           &request_packet);
    TEST_ASSERT_EQUAL(NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_COSE_UNPARSEABLE,
                      auth_result);
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

    // create a cose mac0 signed payload and set it as the packet payload
    // HELLO WORLD
    uint8_t payload_to_secure[11] = {
        0x48, 0x45, 0x4C, 0x4C, 0x4F, 0x20, 0x57, 0x4F, 0x52, 0x4C, 0x44};
    const nexus_cose_mac0_common_macparams_t mac_params = {
        &link_key,
        4, // expected nonce = 5, will cause 4.06 response
        // aad
        {
            request_packet.code,
            (uint8_t*) request_packet.uri_path,
            request_packet.uri_path_len,
        },
        payload_to_secure,
        sizeof(payload_to_secure),
    };

    uint8_t enc_data[NEXUS_CHANNEL_MAX_CBOR_PAYLOAD_SIZE];
    size_t enc_size;
    nexus_cose_error encode_result = nexus_cose_mac0_sign_encode_message(
        &mac_params, enc_data, sizeof(enc_data), &enc_size);
    TEST_ASSERT_EQUAL(NEXUS_COSE_ERROR_NONE, encode_result);

    coap_set_payload(&request_packet, enc_data, enc_size);
    // includes security information
    TEST_ASSERT_EQUAL(request_packet.payload_len, enc_size);

    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);
    TEST_ASSERT_EQUAL(5, sec_data.mode0.nonce);

    nexus_channel_sm_auth_error_t auth_result =
        nexus_channel_authenticate_message(&FAKE_ACCESSORY_ENDPOINT,
                                           &request_packet);
    TEST_ASSERT_EQUAL(
        NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_REQUEST_RECEIVED_WITH_INVALID_NONCE,
        auth_result);
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

    // create a cose mac0 signed payload and set it as the packet payload
    // HELLO WORLD
    uint8_t payload_to_secure[11] = {
        0x48, 0x45, 0x4C, 0x4C, 0x4F, 0x20, 0x57, 0x4F, 0x52, 0x4C, 0x44};
    const nexus_cose_mac0_common_macparams_t mac_params = {
        &link_key,
        6,
        // aad
        {
            request_packet.code,
            (uint8_t*) request_packet.uri_path,
            request_packet.uri_path_len,
        },
        payload_to_secure,
        sizeof(payload_to_secure),
    };

    uint8_t enc_data[NEXUS_CHANNEL_MAX_CBOR_PAYLOAD_SIZE];
    size_t enc_size;
    nexus_cose_error encode_result = nexus_cose_mac0_sign_encode_message(
        &mac_params, enc_data, sizeof(enc_data), &enc_size);
    TEST_ASSERT_EQUAL(NEXUS_COSE_ERROR_NONE, encode_result);

    coap_set_payload(&request_packet, enc_data, enc_size);
    // includes security information
    TEST_ASSERT_EQUAL(request_packet.payload_len, enc_size);

    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);
    TEST_ASSERT_EQUAL(5, sec_data.mode0.nonce);
    nexus_channel_sm_auth_error_t auth_result =
        nexus_channel_authenticate_message(&FAKE_ACCESSORY_ENDPOINT,
                                           &request_packet);
    TEST_ASSERT_EQUAL(
        NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_SENDER_DEVICE_NOT_LINKED,
        auth_result);
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

    // create a cose mac0 signed payload and set it as the packet payload
    // HELLO WORLD
    uint8_t payload_to_secure[11] = {
        0x48, 0x45, 0x4C, 0x4C, 0x4F, 0x20, 0x57, 0x4F, 0x52, 0x4C, 0x44};
    // CAUSE OF FAILURE: MAC in COSE_MAC is incorrect (incorrect key used...)
    const nexus_cose_mac0_common_macparams_t mac_params = {
        &NEXUS_INTEGRITY_CHECK_FIXED_FF_KEY,
        0x01020304,
        // aad
        {
            request_packet.code,
            (uint8_t*) request_packet.uri_path,
            request_packet.uri_path_len,
        },
        payload_to_secure,
        sizeof(payload_to_secure),
    };

    uint8_t enc_data[NEXUS_CHANNEL_MAX_CBOR_PAYLOAD_SIZE];
    size_t enc_size;
    nexus_cose_error encode_result = nexus_cose_mac0_sign_encode_message(
        &mac_params, enc_data, sizeof(enc_data), &enc_size);
    TEST_ASSERT_EQUAL(NEXUS_COSE_ERROR_NONE, encode_result);

    coap_set_payload(&request_packet, enc_data, enc_size);
    // includes security information
    TEST_ASSERT_EQUAL(request_packet.payload_len, enc_size);

    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);
    TEST_ASSERT_EQUAL(5, sec_data.mode0.nonce);
    nexus_channel_sm_auth_error_t auth_result =
        nexus_channel_authenticate_message(&FAKE_ACCESSORY_ENDPOINT,
                                           &request_packet);
    TEST_ASSERT_EQUAL(NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_MAC_INVALID,
                      auth_result);
    // nonce should be unchanged
    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);
    TEST_ASSERT_EQUAL(5, sec_data.mode0.nonce);
}

// because header is not secured, this message won't return an error
// XXX should return some indication that message is not authenticated?
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

    coap_set_payload(&request_packet, payload, strlen("hello world"));
    TEST_ASSERT_EQUAL(request_packet.payload_len, strlen("hello world"));

    uint8_t* original_payload_ptr = request_packet.payload;
    uint32_t original_payload_len = request_packet.payload_len;
    nexus_channel_sm_auth_error_t auth_result =
        nexus_channel_authenticate_message(&FAKE_ACCESSORY_ENDPOINT,
                                           &request_packet);

    TEST_ASSERT_EQUAL(NEXUS_CHANNEL_SM_AUTH_MESSAGE_ERROR_NONE, auth_result);
    // no changes to the payload
    TEST_ASSERT_EQUAL(request_packet.payload_len, strlen("hello world"));
    // payload pointer was unmodified
    TEST_ASSERT_EQUAL(original_payload_ptr, request_packet.payload);
    TEST_ASSERT_EQUAL(original_payload_len, request_packet.payload_len);
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
    nxp_common_request_processing_Ignore();

    struct nx_id fake_id = {0, 12345678};
    nxp_channel_get_nexus_id_ExpectAndReturn(fake_id);
    nxp_channel_network_send_ExpectAnyArgsAndReturn(0);

    nexus_channel_core_process(0);

    TEST_ASSERT_EQUAL(0, oc_process_nevents());
}

nx_channel_error
CALLBACK_test_nexus_channel_sm_do_get_cose_mac0_appended__ok__nxp_channel_network_send(
    const void* const bytes_to_send,
    unsigned int bytes_count,
    const struct nx_id* const source,
    const struct nx_id* const dest,
    bool is_multicast,
    int NumCalls)
{
    (void) NumCalls;
    (void) source;
    (void) dest;
    TEST_ASSERT_FALSE(is_multicast);

    char* uri = "nx/pc";

    // interpret the message sent as oc_message_t
    oc_message_t message = {0};
    message.length = bytes_count;
    memcpy(message.data, bytes_to_send, message.length);

    coap_packet_t coap_pkt[1];
    TEST_ASSERT_EQUAL(
        COAP_NO_ERROR,
        coap_udp_parse_message(coap_pkt, message.data, message.length));
    TEST_ASSERT_EQUAL(COAP_TYPE_NON, coap_pkt->type);
    TEST_ASSERT_EQUAL(COAP_GET, coap_pkt->code);
    // must decrement MID by 1 because every time we call coap_get_mid() it
    // increments and we call it once here
    TEST_ASSERT_EQUAL(coap_get_mid() - 1, coap_pkt->mid);
    TEST_ASSERT_EQUAL(0x40, coap_pkt->token[0]); // mocked value in setUp
    TEST_ASSERT_EQUAL(APPLICATION_COSE_MAC0, coap_pkt->content_format);
    TEST_ASSERT_TRUE(strncmp(uri, coap_pkt->uri_path, 5) == 0);

    // test the encoded payload (generated empirically)
    const uint8_t payload_encoded[16] = {0x84,
                                         0x43,
                                         0xA1,
                                         0x05,
                                         0x01,
                                         0xA0,
                                         0x40,
                                         0x48,
                                         0x7f,
                                         0x4b,
                                         0xbf,
                                         0xb5,
                                         0x0b,
                                         0xb9,
                                         0xe1,
                                         0x3f};

    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        payload_encoded, coap_pkt->payload, sizeof(payload_encoded));

    return NX_CHANNEL_ERROR_NONE;
}

void test_do_get_cose_mac0_appended__ok(void)
{
    const char* uri = "nx/pc";

    // initialize resources
    oc_resource_t* res = oc_ri_get_app_resource_by_uri(
        uri, strlen(uri), NEXUS_CHANNEL_NEXUS_DEVICE_ID);

    // register a new secured resource handler
    nexus_channel_sm_nexus_resource_method_new(res, OC_GET);

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

    // examine the raw data sent to network
    nxp_channel_network_send_StopIgnore();
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);
    // will be called as a result of `oc_do_get`
    nxp_channel_network_send_StubWithCallback(
        CALLBACK_test_nexus_channel_sm_do_get_cose_mac0_appended__ok__nxp_channel_network_send);
    nxp_channel_get_nexus_id_IgnoreAndReturn(linked_id);
    TEST_ASSERT_TRUE(oc_do_get(
        uri, true, &FAKE_ACCESSORY_ENDPOINT, NULL, NULL, LOW_QOS, NULL));

    // one event for outgoing message, one event (transaction idle etimer)
    TEST_ASSERT_EQUAL(2, oc_process_nevents());
    nexus_channel_core_process(0);
}

nx_channel_error
CALLBACK_test_nexus_channel_sm_do_post_cose_mac0_appended__ok__nxp_channel_network_send(
    const void* const bytes_to_send,
    unsigned int bytes_count,
    const struct nx_id* const source,
    const struct nx_id* const dest,
    bool is_multicast,
    int NumCalls)
{
    (void) NumCalls;
    (void) source;
    (void) dest;
    TEST_ASSERT_FALSE(is_multicast);

    char* uri = "nx/pc";

    // interpret the message sent as oc_message_t
    oc_message_t message = {0};
    message.length = bytes_count;
    memcpy(message.data, bytes_to_send, message.length);

    coap_packet_t coap_pkt[1];
    TEST_ASSERT_EQUAL(
        COAP_NO_ERROR,
        coap_udp_parse_message(coap_pkt, message.data, message.length));
    TEST_ASSERT_EQUAL(COAP_TYPE_NON, coap_pkt->type);
    TEST_ASSERT_EQUAL(COAP_POST, coap_pkt->code);
    // must decrement MID by 2 because every time we call coap_get_mid() it
    // increments and we call it once here and once before in
    // `test_do_post_cose_mac0_appended__ok`
    TEST_ASSERT_EQUAL(coap_get_mid() - 2, coap_pkt->mid);
    TEST_ASSERT_EQUAL(0x40, coap_pkt->token[0]); // mocked value in setUp
    TEST_ASSERT_EQUAL(APPLICATION_COSE_MAC0, coap_pkt->content_format);
    TEST_ASSERT_TRUE(strncmp(uri, coap_pkt->uri_path, 5) == 0);

    // test the encoded payload
    const uint8_t payload_encoded[32] = {
        0x84, 0x43, 0xa1, 0x05, 0x01, 0xa0, 0x50, 0xbf, 0x61, 0x64, 0x4b,
        0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x77, 0x6f, 0x72, 0x6c, 0x64,
        0xff, 0x48, 0x41, 0x39, 0x97, 0x90, 0x2f, 0x8f, 0xcf, 0x49};

    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        payload_encoded, coap_pkt->payload, sizeof(payload_encoded));

    return NX_CHANNEL_ERROR_NONE;
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

    // message ID to look up is the last one used (allocated to callback
    // created in `oc_init_post`)
    uint16_t prev_mid = coap_get_mid() - 1;
    coap_transaction_t* t = NULL;
    t = coap_get_transaction_by_mid(prev_mid);

    TEST_ASSERT_TRUE(t != NULL);

    // examine the raw data sent to network
    nxp_channel_network_send_StopIgnore();
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);
    // will be called as a result of `oc_do_post`
    nxp_channel_network_send_StubWithCallback(
        CALLBACK_test_nexus_channel_sm_do_post_cose_mac0_appended__ok__nxp_channel_network_send);

    // This will cause a message to be immediately sent and clear transaction
    TEST_ASSERT_TRUE(oc_do_post(true));
    // (the data pointed to by 't' above is still allocated, not null -
    // we hold on to outbound *request* transactions)
    coap_transaction_t* buffered_t = coap_get_transaction_by_mid(prev_mid);
    TEST_ASSERT_NOT_EQUAL(NULL, buffered_t);
    TEST_ASSERT_EQUAL(buffered_t->message->length, t->message->length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        buffered_t->message->data, t->message->data, t->message->length);

    // one event for OUTBOUND_NETWORK_EVENT, + 1 for `poll_requested` flag
    TEST_ASSERT_EQUAL(2, oc_process_nevents());
    OC_DBG("just before final call of nexus_channel_core_process");
    // This will return the number of seconds until the next OC event -
    // here, it is 5 seconds, the time until
    // `OC_TRANSACTION_CACHED_IDLE_TIMEOUT_SECONDS` (5) is reached, to clear
    // the outbound secured transaction (POST) sent above
    TEST_ASSERT_EQUAL(5, nexus_channel_core_process(0));
}

// Ensure that response from server sent when a secured request is made with
// an invalid nonce is a nonce sync (4.06) with the correct nonce
nx_channel_error
CALLBACK_test_receive_secured_get__server_response_is_nonce_sync_406__server_nonce_unchanged__nxp_channel_network_send(
    const void* const bytes_to_send,
    unsigned int bytes_count,
    const struct nx_id* const source,
    const struct nx_id* const dest,
    bool is_multicast,
    int NumCalls)
{
    (void) NumCalls;
    (void) source;
    (void) dest;
    TEST_ASSERT_FALSE(is_multicast);

    // interpret the message sent as oc_message_t
    oc_message_t message = {0};
    message.length = bytes_count;
    memcpy(message.data, bytes_to_send, message.length);

    coap_packet_t coap_pkt[1];
    TEST_ASSERT_EQUAL(
        COAP_NO_ERROR,
        coap_udp_parse_message(coap_pkt, message.data, message.length));
    TEST_ASSERT_EQUAL(COAP_TYPE_NON, coap_pkt->type);
    TEST_ASSERT_EQUAL(NOT_ACCEPTABLE_4_06, coap_pkt->code);

    // 123 and 0xfa arbitrarily chosen in request message
    TEST_ASSERT_EQUAL(123, coap_pkt->mid);
    TEST_ASSERT_EQUAL(0xfa, coap_pkt->token[0]);
    TEST_ASSERT_EQUAL(APPLICATION_COSE_MAC0, coap_pkt->content_format);
    // response has no URI
    TEST_ASSERT_EQUAL(0, coap_pkt->uri_path_len);
    // but should have a payload
    TEST_ASSERT_NOT_NULL(coap_pkt->payload);
    TEST_ASSERT_EQUAL(17, coap_pkt->payload_len);

    // test the encoded payload (is a COSE MAC0 with nonce=55)
    const uint8_t payload_encoded[31] = {0x84,
                                         0x44,
                                         0xa1,
                                         0x05,
                                         0x18,
                                         0x37,
                                         0xa0,
                                         0x40,
                                         0x48,
                                         0x79,
                                         0x61,
                                         0x7b,
                                         0x6b,
                                         0xcf,
                                         0xbf,
                                         0x26,
                                         0xeb};

    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        payload_encoded, coap_pkt->payload, sizeof(payload_encoded));

    return NX_CHANNEL_ERROR_NONE;
}

// simulate a linked device receiving a secured GET request from other
// device on the link with a too-low nonce, and ensure that a nonce sync
// is sent back ('end to end' test, simulating received network data
// back up to transmitted network data)
void test_receive_secured_get__server_response_is_nonce_sync_406__server_nonce_unchanged(
    void)
{
    const char* uri = "nx/pc";

    // initialize resources
    oc_resource_t* res = oc_ri_get_app_resource_by_uri(
        uri, strlen(uri), NEXUS_CHANNEL_NEXUS_DEVICE_ID);

    // register a new secured resource handler
    nexus_channel_sm_nexus_resource_method_new(res, OC_GET);

    // initializes with no links present
    struct nx_id linked_id = {0};
    linked_id.authority_id = 53932;
    linked_id.device_id = 4244308258;

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    union nexus_channel_link_security_data sec_data = {
        0}; // nonce and sym key are set explicitly below

    // set the nonce to 55
    sec_data.mode0.nonce = 55;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    nxp_common_request_processing_Expect();
    nexus_channel_link_manager_create_link(
        &linked_id,
        CHANNEL_LINK_OPERATING_MODE_CONTROLLER,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nexus_channel_core_process(0);
    TEST_ASSERT_EQUAL(0, oc_process_nevents());

    // Now, simulate receiving a message from the linked accessory
    // that requests the secured resource/method (GET /nx/pc), but has
    // a too-low nonce.
    coap_packet_t request_packet = {0};
    // initialize packet: GET with arbitrary message ID
    coap_udp_init_message(&request_packet, COAP_TYPE_NON, OC_GET, 123);
    // set a 1-byte token per Nexus Channel Core spec
    uint8_t token_val = 0xfa;
    coap_set_token(&request_packet, &token_val, 1);
    // set the request URI path and content format
    coap_set_header_uri_path(&request_packet, "/nx/pc", strlen("/nx/pc"));
    coap_set_header_content_format(&request_packet, APPLICATION_COSE_MAC0);

    // create a cose mac0 signed payload and set it as the packet payload
    const nexus_cose_mac0_common_macparams_t mac_params = {
        &link_key,
        54, // 54 < 55, should trigger a nonce sync
        // aad
        {
            request_packet.code,
            (uint8_t*) request_packet.uri_path,
            request_packet.uri_path_len,
        },
        NULL, // get request, no payload
        0,
    };

    uint8_t enc_data[NEXUS_CHANNEL_MAX_CBOR_PAYLOAD_SIZE];
    size_t enc_size;
    nexus_cose_error encode_result = nexus_cose_mac0_sign_encode_message(
        &mac_params, enc_data, sizeof(enc_data), &enc_size);
    TEST_ASSERT_EQUAL(NEXUS_COSE_ERROR_NONE, encode_result);

    coap_set_payload(&request_packet, enc_data, enc_size);
    // includes security information
    TEST_ASSERT_EQUAL(request_packet.payload_len, enc_size);

    if (G_OC_MESSAGE)
    {
        // set the message length based on the result of data serialization
        G_OC_MESSAGE->length =
            coap_serialize_message(&request_packet, G_OC_MESSAGE->data);
    }
    TEST_ASSERT_NOT_NULL(G_OC_MESSAGE);

    // need to simulate receiving a message *from* somewhere
    oc_endpoint_copy(&G_OC_MESSAGE->endpoint, &FAKE_ACCESSORY_ENDPOINT);

    // Simulate receiving this secured GET from other device on the link
    oc_network_event(G_OC_MESSAGE);

    nxp_common_request_processing_Expect();

    // should send a request to the network
    // examine the raw data sent to network
    nxp_channel_network_send_StopIgnore();
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);
    nxp_channel_get_nexus_id_IgnoreAndReturn(linked_id);

    // will be called as a result of `oc_do_post`
    nxp_channel_network_send_StubWithCallback(
        CALLBACK_test_receive_secured_get__server_response_is_nonce_sync_406__server_nonce_unchanged__nxp_channel_network_send);

    // one pending event for previously received message (from oc_network_event)
    TEST_ASSERT_EQUAL(1, oc_process_nevents());
    nexus_channel_core_process(0);

    // Confirm existing link has not changed its nonce (55)
    nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                       &sec_data.mode0);
    TEST_ASSERT_EQUAL(55, sec_data.mode0.nonce);
}

static void nexus_channel_test_fake_resource_get_handler_valid(
    oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
    (void) user_data;
    (void) request;
    (void) interfaces;

    oc_rep_begin_root_object();
    oc_rep_set_uint(root, 'fakepayloadkey', 5);
    oc_rep_end_root_object();

    oc_send_response(request, OC_STATUS_OK);
}

// Checks that the payload doesn't appear to be COSE MAC0, as the
// client response handler should not see the security wrapper
void _get_nx_pc_response_handler_verify_payload_is_not_cose_mac0(
    nx_channel_client_response_t* response)
{
    // equal to payload constructed in
    // `nexus_channel_test_fake_resource_get_handler_valid`
    // which is not a MAC0 payload
    oc_rep_t* rep = response->payload;
    TEST_ASSERT_EQUAL(OC_REP_INT, rep->type);
    TEST_ASSERT_EQUAL(0, strcmp(oc_string(rep->name), "'fakepayloadkey'"));
    TEST_ASSERT_EQUAL(5, rep->value.integer);
    TEST_ASSERT_EQUAL(NULL, rep->next);
}

nx_channel_error
CALLBACK_test_do_secured_get__server_response_nonce_is_valid__client_app_receives_response__nx_channel_network_send(
    const void* const bytes_to_send,
    unsigned int bytes_count,
    const struct nx_id* const source,
    const struct nx_id* const dest,
    bool is_multicast,
    int NumCalls)
{
    (void) source;
    (void) dest;
    TEST_ASSERT_FALSE(is_multicast);

    // Note: Breakpoint here and examine bytes_to_send and bytes_count
    // to get the 'raw' data which would be sent on the wire

    // interpret the message sent as oc_message_t
    oc_message_t message = {0};
    message.length = bytes_count;
    memcpy(message.data, bytes_to_send, message.length);

    coap_packet_t coap_pkt[1];
    TEST_ASSERT_EQUAL(
        COAP_NO_ERROR,
        coap_udp_parse_message(coap_pkt, message.data, message.length));

    // Here, we are sending a request outboung to `nx/myfakeuri` with a valid
    // nonce
    if (NumCalls == 0)
    {
        TEST_ASSERT_EQUAL(COAP_TYPE_NON, coap_pkt->type);
        TEST_ASSERT_EQUAL(COAP_GET, coap_pkt->code);

        TEST_ASSERT_EQUAL(APPLICATION_COSE_MAC0, coap_pkt->content_format);
        // 'nx/pc'
        const char* uri_path;
        size_t path_len = coap_get_header_uri_path(coap_pkt, &uri_path);
        TEST_ASSERT_EQUAL(10, path_len);
        TEST_ASSERT_EQUAL_STRING_LEN("nx/fakeuri", uri_path, 10);
        // has COSE MAC0 payload (secured GET message)
        TEST_ASSERT_EQUAL(17, coap_pkt->payload_len);

        // test the encoded payload (is a COSE MAC0 for GET, no payload)
        const uint8_t payload_encoded[17] = {0x84,
                                             0x44,
                                             0xa1,
                                             0x05,
                                             0x18,
                                             0x1A,
                                             0xa0,
                                             0x40,
                                             0x48,
                                             0xa3,
                                             0xeb,
                                             0x39,
                                             0x20,
                                             0xde,
                                             0x15,
                                             0xe5,
                                             0x42};

        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            payload_encoded, coap_pkt->payload, sizeof(payload_encoded));

        // In this test, loop the outbound data back to the same device under
        // test (which is also acting as a server for the secured request)
        // We have to set up another expect here to handle the second send
        // (which will be handled within this stub in the `NumCalls == 1`
        // block below)

        nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);
        nx_channel_network_receive(bytes_to_send, bytes_count, source);
    }
    // here, we are sending a response to the message sent in `NumCalls == 0`,
    // with a valid, secured response payload.
    else if (NumCalls == 1)
    {
        // data being sent here is a 'response' to the initial request
        TEST_ASSERT_EQUAL(COAP_TYPE_NON, coap_pkt->type);

        // response generated in
        // `nexus_channel_test_fake_resource_get_handler_valid`
        TEST_ASSERT_EQUAL(CONTENT_2_05, coap_pkt->code);
        TEST_ASSERT_EQUAL(APPLICATION_COSE_MAC0, coap_pkt->content_format);

        // response has zero URI path length
        const char* uri_path;
        size_t path_len = coap_get_header_uri_path(coap_pkt, &uri_path);
        TEST_ASSERT_EQUAL(0, path_len);
        // has COSE MAC0 payload (secured GET message)
        TEST_ASSERT_EQUAL(37, coap_pkt->payload_len);

        // test the encoded payload (is a COSE MAC0 for server response to GET)
        // [h'A105181A', {}, h'BF702766616B657061796C6F61646B65792705FF',
        // h'C77322FC22D268AB'] encapsulated payload (BF702766...) decodes as:
        // {"'fakepayloadkey'": 5}
        const uint8_t payload_encoded[37] = {
            0x84, 0x44, 0xA1, 0x05, 0x18, 0x1A, 0xA0, 0x54, 0xBF, 0x70,
            0x27, 0x66, 0x61, 0x6B, 0x65, 0x70, 0x61, 0x79, 0x6C, 0x6F,
            0x61, 0x64, 0x6B, 0x65, 0x79, 0x27, 0x05, 0xFF, 0x48, 0xC7,
            0x73, 0x22, 0xFC, 0x22, 0xD2, 0x68, 0xAB};

        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            payload_encoded, coap_pkt->payload, sizeof(payload_encoded));

        // here, we 'receive' the sent data, which will be passed to the
        // response handler. No further messages are sent.
        nx_channel_network_receive(bytes_to_send, bytes_count, source);
    }
    else
    {
        OC_WRN("Value of calls %d\n", NumCalls);
        // this callback is used only twice - if we reach this 'else'
        // block, it means that the response we sent from the previous
        // loop has triggered *another* response. A response should
        // never trigger a response (even a nonce sync), only requests
        // should trigger responses.
        TEST_ASSERT_FALSE(1);
    }
    return NX_CHANNEL_ERROR_NONE;
}

// Simulate a linked accessory (linked to itself as a server) which sends
// a secured GET request (as a client) with a valid nonce to a test resource.
// The server implementation for the test resource then accepts the message
// (gets to the server's handler for the request), and a response is
// sent back to the client.
//
// Test also verifies that both the outbound client request and the outbound
// server response are correctly packed as secured COSE MAC0 messages.
void test_do_secured_get__server_response_nonce_is_valid__client_app_receives_response(
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

    // set the local nonce to 25
    sec_data.mode0.nonce = 25;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    nxp_common_request_processing_Expect();
    // this device will be linked to
    nexus_channel_link_manager_create_link(
        &linked_id,
        CHANNEL_LINK_OPERATING_MODE_ACCESSORY,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nexus_channel_core_process(0);
    TEST_ASSERT_EQUAL(0, oc_process_nevents());

    // Now, simulate sending a secured request from this linked accessory to the
    // controller to the secured resource/method (GET /nx/pc).
    nxp_common_request_processing_Expect();
    nx_channel_error result = nx_channel_do_get_request_secured(
        "nx/fakeuri",
        &linked_id,
        NULL,
        // *RESPONSE* handler
        &_get_nx_pc_response_handler_verify_payload_is_not_cose_mac0,
        NULL);
    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_NONE, result);
    // one event for OUTBOUND_NETWORK_EVENT, + 1 for `poll_requested` flag
    TEST_ASSERT_EQUAL(2, oc_process_nevents());

    // serve the same resource that we made a request to GET, and secure the
    // GET
    const struct nx_channel_resource_props fake_res_props = {
        .uri = "nx/fakeuri",
        .resource_type = "angaza.com.nexus.fake_resource",
        .rtr = 65001,
        .num_interfaces = 2,
        .if_masks = if_mask_arr,
        // *REQUEST* handler
        .get_handler = nexus_channel_test_fake_resource_get_handler_valid,
        .get_secured = true, // we are testing the secured GET handler behavior
        .post_handler = NULL,
        .post_secured = false};

    nx_channel_error reg_result = nx_channel_register_resource(&fake_res_props);
    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_NONE, reg_result);

    // will be called as a result of `oc_do_get`
    // This callback is configured to check the outbound data for expected
    // values, then 'receive' the data
    // Here, nonce is correct (25), and we'll call `get_handler_valid`
    nxp_channel_network_send_Stub(
        CALLBACK_test_do_secured_get__server_response_nonce_is_valid__client_app_receives_response__nx_channel_network_send);

    // Still haven't polled to handle pending processes
    TEST_ASSERT_EQUAL(2, oc_process_nevents());

    // Calling `core_process` will cause our earlier call of
    // `nx_channel_do_get_request_secured` to be handled, sending an outbound
    // message. We'll capture and examine that message (and subsequent calls)
    // inside our `nxp_channel_network_send_Stub`, but we need these expects
    // here for the first call.
    nxp_common_request_processing_Ignore();
    nxp_channel_get_nexus_id_IgnoreAndReturn(linked_id);
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);

    nexus_channel_core_process(0);
    TEST_ASSERT_EQUAL(0, oc_process_nevents());
}

// simple response handler to test that a request which will cause an
// error response will not be passed down to the application layer response
// handler
void _get_nx_pc_response_handler_should_not_be_called(
    nx_channel_client_response_t* response)
{
    (void) response;
    TEST_ASSERT_FALSE(1);
}

nx_channel_error
CALLBACK_test_do_secured_get__server_response_nonce_is_invalid__client_ignores_response__nx_channel_network_send(
    const void* const bytes_to_send,
    unsigned int bytes_count,
    const struct nx_id* const source,
    const struct nx_id* const dest,
    bool is_multicast,
    int NumCalls)
{
    (void) source;
    (void) dest;
    TEST_ASSERT_FALSE(is_multicast);

    // Note: Breakpoint here and examine bytes_to_send and bytes_count
    // to get the 'raw' data which would be sent on the wire

    // interpret the message sent as oc_message_t
    oc_message_t message = {0};
    message.length = bytes_count;
    memcpy(message.data, bytes_to_send, message.length);

    coap_packet_t coap_pkt[1];
    TEST_ASSERT_EQUAL(
        COAP_NO_ERROR,
        coap_udp_parse_message(coap_pkt, message.data, message.length));

    // this is the outbound client request - a valid message
    if (NumCalls == 0)
    {
        TEST_ASSERT_EQUAL(COAP_TYPE_NON, coap_pkt->type);
        TEST_ASSERT_EQUAL(COAP_GET, coap_pkt->code);

        TEST_ASSERT_EQUAL(APPLICATION_COSE_MAC0, coap_pkt->content_format);
        // 'nx/pc'
        const char* uri_path;
        size_t path_len = coap_get_header_uri_path(coap_pkt, &uri_path);
        TEST_ASSERT_EQUAL(10, path_len);
        TEST_ASSERT_EQUAL_STRING_LEN("nx/fakeuri", uri_path, 10);
        // has COSE MAC0 payload (secured GET message)
        TEST_ASSERT_EQUAL(17, coap_pkt->payload_len);

        // test the encoded payload (is a COSE MAC0 for GET with nonce=26)
        const uint8_t payload_encoded[17] = {0x84,
                                             0x44,
                                             0xA1,
                                             0x05,
                                             0x18,
                                             0x1a,
                                             0xA0,
                                             0x40,
                                             0x48,
                                             0xa3,
                                             0xeb,
                                             0x39,
                                             0x20,
                                             0xde,
                                             0x15,
                                             0xe5,
                                             0x42};

        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            payload_encoded, coap_pkt->payload, sizeof(payload_encoded));

        // In this test, loop the outbound data back to the same device under
        // test (which is also acting as a server for the secured request)
        // We have to set up another expect here to handle the second send
        // (which will be handled within this stub in the `NumCalls == 1`
        // block below)
        nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);
        nx_channel_network_receive(bytes_to_send, bytes_count, source);
    }
    else if (NumCalls == 1)
    {
        // response generated in
        // `nexus_channel_test_fake_resource_get_handler_valid`
        // data being sent here is a 'response' to the initial request
        TEST_ASSERT_EQUAL(COAP_TYPE_NON, coap_pkt->type);
        TEST_ASSERT_EQUAL(CONTENT_2_05, coap_pkt->code);
        TEST_ASSERT_EQUAL(APPLICATION_COSE_MAC0, coap_pkt->content_format);

        // response has zero URI path length
        const char* uri_path;
        size_t path_len = coap_get_header_uri_path(coap_pkt, &uri_path);
        TEST_ASSERT_EQUAL(0, path_len);
        // has COSE MAC0 payload (secured GET message)
        TEST_ASSERT_EQUAL(37, coap_pkt->payload_len);

        // test the encoded payload (is a CoAP message with response code
        // 0x44 = decimal 68 = "CHANGED_2_04"
        const uint8_t payload_encoded[37] = {
            0x84, 0x44, 0xa1, 0x05, 0x18, 0x1a, 0xa0, 0x54, 0xbf, 0x70,
            0x27, 0x66, 0x61, 0x6b, 0x65, 0x70, 0x61, 0x79, 0x6c, 0x6f,
            0x61, 0x64, 0x6b, 0x65, 0x79, 0x27, 0x05, 0xff, 0x48, 0xc7,
            0x73, 0x22, 0xfc, 0x22, 0xd2, 0x68, 0xab};

        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            payload_encoded, coap_pkt->payload, sizeof(payload_encoded));

        // arbitrary ID from related test
        struct nx_id linked_id = {0};
        linked_id.authority_id = 53932;
        linked_id.device_id = 4244308258;
        union nexus_channel_link_security_data sec_data;

        bool sec_data_exists =
            nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                               &sec_data.mode0);
        TEST_ASSERT_TRUE(sec_data_exists);

        TEST_ASSERT_EQUAL(26, sec_data.mode0.nonce);

        // response message has a nonce of 26, same as the outbound request.
        // Normally, we'd accept this response:
        // sent with nonce = 26 (local nonce = 26)
        // response nonce = 26 (local nonce = 26, accepted)
        // but, since here we're overriding the local nonce to be 27, we won't
        // call the response handler - and should instead trigger a 'nonce sync'
        // response.
        nexus_channel_link_manager_set_security_data_auth_nonce(&linked_id, 27);

        // one from calling `nx_channel_network_receive`
        nx_channel_network_receive(bytes_to_send, bytes_count, source);
    }
    else
    {
        // here, we are sending the nonce sync response back in response
        // to the request. Since we locally updated the nonce to 27 (see above),
        // we expect to see 27 as the value sent here.
        TEST_ASSERT_EQUAL(COAP_TYPE_NON, coap_pkt->type);
        TEST_ASSERT_EQUAL(NOT_ACCEPTABLE_4_06, coap_pkt->code);
        TEST_ASSERT_EQUAL(APPLICATION_COSE_MAC0, coap_pkt->content_format);

        // response has zero URI path length
        const char* uri_path;
        size_t path_len = coap_get_header_uri_path(coap_pkt, &uri_path);
        TEST_ASSERT_EQUAL(0, path_len);
        // has COSE MAC0 payload (secured GET message)
        TEST_ASSERT_EQUAL(17, coap_pkt->payload_len);

        // test the encoded payload (is a COSE MAC0 for server response to GET)
        const uint8_t payload_encoded[17] = {0x84,
                                             0x44,
                                             0xa1,
                                             0x05,
                                             0x18,
                                             0x1b,
                                             0xa0,
                                             0x40,
                                             0x48,
                                             0x01,
                                             0x6f,
                                             0x82,
                                             0x27,
                                             0xf0,
                                             0x65,
                                             0x58,
                                             0x63};

        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            payload_encoded, coap_pkt->payload, sizeof(payload_encoded));
    }

    return NX_CHANNEL_ERROR_NONE;
}

// Simulate a linked accessory (linked to itself as a server) which sends
// a secured GET request (as a client) with a valid nonce to a test resource.
// The server implementation for the test resource then accepts the message
// (gets to the server's handler for the request), and a response is
// sent back to the client. HOWEVER, the server response is invalid (invalid
// nonce) and is dropped by the client.
//
// 1. Send valid request message (GET)
// 2. Receive valid request message from #1, generate a valid 2xx responmse
// 3. Manually update the security data/nonce on the simulated device
// 4. Send back the response created in #2
// 5. Requester silently drops the response (response with invalid nonce is
// silently ignored)
//
// Test does not verify message contents already confirmed by
// `test_do_secured_get__server_response_nonce_is_valid__client_app_receives_response`
void test_do_secured_get__server_response_nonce_is_invalid__client_ignores_response(
    void)
{
    // initializes with no links present
    struct nx_id linked_id = {0};
    linked_id.authority_id = 53932;
    linked_id.device_id = 4244308258;

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    // not testing where this is called in this test
    nxp_common_request_processing_Ignore();

    union nexus_channel_link_security_data sec_data = {
        0}; // nonce and sym key are set explicitly below

    // set the local nonce to 25
    sec_data.mode0.nonce = 25;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    // this device will be linked to
    nexus_channel_link_manager_create_link(
        &linked_id,
        CHANNEL_LINK_OPERATING_MODE_ACCESSORY,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nexus_channel_core_process(0);
    TEST_ASSERT_EQUAL(0, oc_process_nevents());

    // serve a fake resource we will attempt to get
    // GET
    const struct nx_channel_resource_props fake_res_props = {
        .uri = "nx/fakeuri",
        .resource_type = "angaza.com.nexus.fake_resource",
        .rtr = 65001,
        .num_interfaces = 2,
        .if_masks = if_mask_arr,
        // *REQUEST* handler
        .get_handler = nexus_channel_test_fake_resource_get_handler_valid,
        .get_secured = true, // we are testing the secured GET handler behavior
        .post_handler = NULL,
        .post_secured = false};

    nx_channel_error reg_result = nx_channel_register_resource(&fake_res_props);
    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_NONE, reg_result);
    TEST_ASSERT_EQUAL(0, oc_process_nevents());

    // Now, simulate sending a secured request from this linked accessory to the
    // controller to the secured resource/method (GET /nx/pc)
    nxp_channel_get_nexus_id_IgnoreAndReturn(linked_id);
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);

    nx_channel_error result = nx_channel_do_get_request_secured(
        "nx/fakeuri",
        &linked_id,
        NULL,
        // *RESPONSE* handler, we won't call this as the response should
        // be a nonce sync, not a valid response -- so the security layer
        // should capture the response instead of invoking the response handler
        &_get_nx_pc_response_handler_should_not_be_called,
        NULL);
    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_NONE, result);
    // one event for OUTBOUND_NETWORK_EVENT, + 1 for `poll_requested` flag
    TEST_ASSERT_EQUAL(2, oc_process_nevents());

    // should send a request to the network
    // examine the raw data sent to network
    nxp_channel_network_send_StopIgnore();

    // will be called as a result of `oc_do_get`
    // This callback is configured to check the outbound data for expected
    // values, then 'receive' the data
    nxp_channel_network_send_Stub(
        CALLBACK_test_do_secured_get__server_response_nonce_is_invalid__client_ignores_response__nx_channel_network_send);

    // Still waiting to send outbound message
    TEST_ASSERT_EQUAL(2, oc_process_nevents());
    nexus_channel_core_process(0);
    TEST_ASSERT_EQUAL(0, oc_process_nevents());
}

nx_channel_error
CALLBACK_test_do_secured_get__server_response_nonce_nearing_overflow_should_reset__client_resets_local_nonce__nx_channel_network_send(
    const void* const bytes_to_send,
    unsigned int bytes_count,
    const struct nx_id* const source,
    const struct nx_id* const dest,
    bool is_multicast,
    int NumCalls)
{
    (void) source;
    (void) dest;
    TEST_ASSERT_FALSE(is_multicast);

    // Note: Breakpoint here and examine bytes_to_send and bytes_count
    // to get the 'raw' data which would be sent on the wire

    // interpret the message sent as oc_message_t
    oc_message_t message = {0};
    message.length = bytes_count;
    memcpy(message.data, bytes_to_send, message.length);

    coap_packet_t coap_pkt[1];
    TEST_ASSERT_EQUAL(
        COAP_NO_ERROR,
        coap_udp_parse_message(coap_pkt, message.data, message.length));

    // this is the original outbound client request - a valid message
    if (NumCalls == 0)
    {
        TEST_ASSERT_EQUAL(COAP_TYPE_NON, coap_pkt->type);
        TEST_ASSERT_EQUAL(COAP_GET, coap_pkt->code);

        TEST_ASSERT_EQUAL(APPLICATION_COSE_MAC0, coap_pkt->content_format);
        // 'nx/pc'
        const char* uri_path;
        size_t path_len = coap_get_header_uri_path(coap_pkt, &uri_path);
        TEST_ASSERT_EQUAL(10, path_len);
        TEST_ASSERT_EQUAL_STRING_LEN("nx/fakeuri", uri_path, 10);
        // has COSE MAC0 payload (secured GET message)
        TEST_ASSERT_EQUAL(20, coap_pkt->payload_len);

        // test the encoded payload (is a COSE MAC0 for GET with
        // nonce=4294967265 - nonce NV write interval)
        const uint8_t payload_encoded[20] = {
            0x84, 0x47, 0xA1, 0x05, 0x1A, 0xFF, 0xFF, 0xFF, 0xC1, 0xA0,
            0x40, 0x48, 0x78, 0x27, 0x5f, 0xfe, 0x80, 0x85, 0x70, 0x04};

        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            payload_encoded, coap_pkt->payload, sizeof(payload_encoded));

        // In this test, loop the outbound data back to the same device under
        // test (which is also acting as a server for the secured request)
        // We have to set up another expect here to handle the second send
        // (which will be handled within this stub in the `NumCalls == 1`
        // block below)
        nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);
        nx_channel_network_receive(bytes_to_send, bytes_count, source);
    }
    else if (NumCalls == 1)
    {
        // response being sent back here by the 'server' receiving the
        // previous request will be a 406 nonce sync with a special reset value
        TEST_ASSERT_EQUAL(COAP_TYPE_NON, coap_pkt->type);
        TEST_ASSERT_EQUAL(NOT_ACCEPTABLE_4_06, coap_pkt->code);
        TEST_ASSERT_EQUAL(APPLICATION_COSE_MAC0, coap_pkt->content_format);

        // response has zero URI path length
        const char* uri_path;
        size_t path_len = coap_get_header_uri_path(coap_pkt, &uri_path);
        TEST_ASSERT_EQUAL(0, path_len);
        // has COSE MAC0 payload (secured GET message)
        TEST_ASSERT_EQUAL(20, coap_pkt->payload_len);

        // test the encoded payload (is a CoAP message with response code 406
        // and nonce of 0xFFFFFFFF)
        const uint8_t payload_encoded[37] = {
            0x84, 0x47, 0xa1, 0x05, 0x1a, 0xff, 0xff, 0xff, 0xff, 0xa0,
            0x40, 0x48, 0x91, 0x7c, 0x6d, 0x28, 0x4f, 0x7e, 0xa3, 0x93};

        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            payload_encoded, coap_pkt->payload, sizeof(payload_encoded));

        // arbitrary ID from related test
        struct nx_id linked_id = {0};
        linked_id.authority_id = 53932;
        linked_id.device_id = 4244308258;
        union nexus_channel_link_security_data sec_data;

        bool sec_data_exists =
            nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                               &sec_data.mode0);

        TEST_ASSERT_TRUE(sec_data_exists);
        // because server and client are the same device here, the server
        // sending the nonce sync has already reset the link nonce to 0.
        TEST_ASSERT_EQUAL(0, sec_data.mode0.nonce);

        // response message has a nonce of UINT32_MAX, as its a special
        // nonce sync 'reset'.
        // Lets set our local nonce to something not-zero, and ensure that
        // we reset it to 0 after receiving this message
        nexus_channel_link_manager_set_security_data_auth_nonce(&linked_id, 30);

        // Now, receive the nonce sync reset that is being sent (loop it back
        // to the 'client'), which should update its nonce from 30 to 0
        nx_channel_network_receive(bytes_to_send, bytes_count, source);
    }
    else if (NumCalls == 2)
    {
        // here, the client is *re-requesting* the original request, but
        // with an updated nonce in response to a 406 nonce sync.
        TEST_ASSERT_EQUAL(COAP_TYPE_NON, coap_pkt->type);
        TEST_ASSERT_EQUAL(COAP_GET, coap_pkt->code);

        TEST_ASSERT_EQUAL(APPLICATION_COSE_MAC0, coap_pkt->content_format);
        const char* uri_path;
        size_t path_len = coap_get_header_uri_path(coap_pkt, &uri_path);
        TEST_ASSERT_EQUAL(10, path_len);
        TEST_ASSERT_EQUAL_STRING_LEN("nx/fakeuri", uri_path, 10);
        // has COSE MAC0 payload (secured GET message)
        TEST_ASSERT_EQUAL(16, coap_pkt->payload_len);

        // test the encoded payload (is a COSE MAC0 for GET with
        // updated nonce=1)
        const uint8_t payload_encoded[16] = {0x84,
                                             0x43,
                                             0xA1,
                                             0x05,
                                             0x01,
                                             0xA0,
                                             0x40,
                                             0x48,
                                             0x96,
                                             0x6c,
                                             0xad,
                                             0x41,
                                             0xff,
                                             0x3a,
                                             0x13,
                                             0x84};

        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            payload_encoded, coap_pkt->payload, sizeof(payload_encoded));

        // loop the requested back to the same device under test -- here,
        // this is the client 'resending' the request after updating its nonce
        // to the server again (which should send a valid response next,
        // not another nonce sync)
        nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);
        nx_channel_network_receive(bytes_to_send, bytes_count, source);
    }
    else if (NumCalls == 3)
    {
        // second response being sent back by the server, in response to
        // the second request sent by the client with the updated nonce.
        TEST_ASSERT_EQUAL(COAP_TYPE_NON, coap_pkt->type);
        TEST_ASSERT_EQUAL(CONTENT_2_05, coap_pkt->code);
        TEST_ASSERT_EQUAL(APPLICATION_COSE_MAC0, coap_pkt->content_format);

        // response has zero URI path length
        const char* uri_path;
        size_t path_len = coap_get_header_uri_path(coap_pkt, &uri_path);
        TEST_ASSERT_EQUAL(0, path_len);
        // has COSE MAC0 payload (secured CONTENT_2_05 response)
        TEST_ASSERT_EQUAL(36, coap_pkt->payload_len);

        const uint8_t payload_encoded[36] = {
            0x84, 0x43, 0xa1, 0x05, 0x01, 0xa0, 0x54, 0xbf, 0x70,
            0x27, 0x66, 0x61, 0x6b, 0x65, 0x70, 0x61, 0x79, 0x6c,
            0x6f, 0x61, 0x64, 0x6b, 0x65, 0x79, 0x27, 0x05, 0xff,
            0x48, 0xb7, 0xd7, 0xf7, 0xc8, 0x0d, 0x7f, 0x49, 0x68};

        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            payload_encoded, coap_pkt->payload, sizeof(payload_encoded));

        // arbitrary ID from related test
        struct nx_id linked_id = {0};
        linked_id.authority_id = 53932;
        linked_id.device_id = 4244308258;
        union nexus_channel_link_security_data sec_data;

        bool sec_data_exists =
            nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                               &sec_data.mode0);

        TEST_ASSERT_TRUE(sec_data_exists);
        // still have a nonce of 1
        TEST_ASSERT_EQUAL(1, sec_data.mode0.nonce);

        // send valid reply back to the client after nonce updated
        // we do *not* expect another nxp_channel_network_send to be called
        nx_channel_network_receive(bytes_to_send, bytes_count, source);
    }
    else
    {
        // should be no other sent NXC messages
        TEST_ASSERT_FALSE(1);
    }

    return NX_CHANNEL_ERROR_NONE;
}

// simple response handler to test that a request which will cause an
// error response will not be passed down to the application layer response
// handler
void _get_nx_pc_response_handler_finally_receives_ok(
    nx_channel_client_response_t* response)
{
    // equal to payload constructed in
    // `nexus_channel_test_fake_resource_get_handler_valid`
    // which is not a MAC0 payload
    oc_rep_t* rep = response->payload;
    TEST_ASSERT_EQUAL(OC_REP_INT, rep->type);
    TEST_ASSERT_EQUAL(0, strcmp(oc_string(rep->name), "'fakepayloadkey'"));
    TEST_ASSERT_EQUAL(5, rep->value.integer);
    TEST_ASSERT_EQUAL(NULL, rep->next);
}

// Simulate a linked accessory (linked to itself as a server) which sends
// a secured GET request (as a client) with a valid nonce to a test resource.
// However, before sending the GET request, set the link nonce to a very large
// value (UINT32_MAX - 5).
//
// This should cause the server to accept the message as valid, but respond
// with a nonce sync to reset the nonce to 0. We check that the message sent
// to the client is this value, and that no subsequent message is sent again.
//
// We may need to update this test once nonce sync has 'automatic retries' to
// account for the clients retry with its new nonce of 0.
//
// 1. Send valid request message (GET) with high valued nonce (as client)
// 2. Receive valid request message from #1, set local nonce to 0, generate a
// 406 response with 'reset nonce' value (as server)
// 3. Manually update the security data/nonce on the client device (set nonce to
// some nonzero value)
// 4. Send back the response created in #2 to the original client
// 5. Client receives the nonce sync, and resets its nonce to 0.
// 6. Client *resends* the previously sent request (from step 1) with a correct
// nonce.
// 7. Server receives the valid request, processes it, sends a valid response
// (not nonce sync)
// 8. Client receives and processes the valid response.

void test_do_secured_get__server_response_nonce_nearing_overflow_should_reset__client_resets_local_nonce(
    void)
{
    // initializes with no links present
    struct nx_id linked_id = {0};
    linked_id.authority_id = 53932;
    linked_id.device_id = 4244308258;

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    // not testing where this is called in this test
    nxp_common_request_processing_Ignore();

    union nexus_channel_link_security_data sec_data = {
        0}; // nonce and sym key are set explicitly below

    // set the local nonce to 'a high value' nearing rollover.
    sec_data.mode0.nonce =
        UINT32_MAX -
        NEXUS_CHANNEL_LINK_SECURITY_NONCE_NV_STORAGE_INTERVAL_COUNT + 1;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    // this device will be linked to
    nexus_channel_link_manager_create_link(
        &linked_id,
        CHANNEL_LINK_OPERATING_MODE_ACCESSORY,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nexus_channel_core_process(0);
    TEST_ASSERT_EQUAL(0, oc_process_nevents());

    // serve a fake resource we will attempt to get
    // GET
    const struct nx_channel_resource_props fake_res_props = {
        .uri = "nx/fakeuri",
        .resource_type = "angaza.com.nexus.fake_resource",
        .rtr = 65001,
        .num_interfaces = 2,
        .if_masks = if_mask_arr,
        // *REQUEST* handler
        .get_handler = nexus_channel_test_fake_resource_get_handler_valid,
        .get_secured = true, // we are testing the secured GET handler behavior
        .post_handler = NULL,
        .post_secured = false};

    nx_channel_error reg_result = nx_channel_register_resource(&fake_res_props);
    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_NONE, reg_result);
    TEST_ASSERT_EQUAL(0, oc_process_nevents());

    // Now, simulate sending a secured request from this linked accessory to the
    // controller to the secured resource/method (GET /nx/pc)
    nxp_channel_get_nexus_id_IgnoreAndReturn(linked_id);
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);

    nx_channel_error result = nx_channel_do_get_request_secured(
        "nx/fakeuri",
        &linked_id,
        NULL,
        // *RESPONSE* handler, we will eventually call this - once the
        // nonce sync completes and we retry (automatically) with a new nonce.
        &_get_nx_pc_response_handler_finally_receives_ok,
        NULL);
    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_NONE, result);
    // one event for OUTBOUND_NETWORK_EVENT, + 1 for `poll_requested` flag
    TEST_ASSERT_EQUAL(2, oc_process_nevents());

    // should send a request to the network
    // examine the raw data sent to network
    nxp_channel_network_send_StopIgnore();

    // will be called as a result of `oc_do_get`
    // This callback is configured to check the outbound data for expected
    // values, then 'receive' the data
    nxp_channel_network_send_Stub(
        CALLBACK_test_do_secured_get__server_response_nonce_nearing_overflow_should_reset__client_resets_local_nonce__nx_channel_network_send);

    // Still waiting to send outbound message
    TEST_ASSERT_EQUAL(2, oc_process_nevents());
    nexus_channel_core_process(0);

    // ensure there are no pending processes, and all message buffers are empty
    TEST_ASSERT_EQUAL(0, oc_process_nevents());
    // transactions
    TEST_ASSERT_EQUAL(COAP_MAX_OPEN_TRANSACTIONS,
                      coap_transactions_free_count());
    // client callbacks
    TEST_ASSERT_EQUAL(OC_MAX_NUM_CONCURRENT_REQUESTS + 1,
                      oc_ri_client_cb_free_count());
    // oc_message incoming/outgoing buffers
    TEST_ASSERT_EQUAL(OC_MAX_NUM_CONCURRENT_REQUESTS,
                      oc_buffer_outgoing_free_count());
    // 1 incoming buffer consumed within test setup function...
    TEST_ASSERT_EQUAL(OC_MAX_NUM_CONCURRENT_REQUESTS - 1,
                      oc_buffer_incoming_free_count());

    // after this nonce sync reset occurs, the new nonce is 1 - we were
    // reset to 0 upon receiving the nonce sync,
    bool sec_data_exists = nexus_channel_link_manager_security_data_from_nxid(
        &linked_id, &sec_data.mode0);

    TEST_ASSERT_TRUE(sec_data_exists);

    // because server and client are the same device here, the server
    // sending the nonce sync has already reset the link nonce to 0. It then
    // sent the reset nonce sync message to the 'client', which will then
    // reset its own local nonce to 0 after receiving it.
    // The client will *retry* the previous request with an increased
    // nonce of 1 because the server expects to receive messages with
    // nonces greater than its own.
    TEST_ASSERT_EQUAL(1, sec_data.mode0.nonce);
}

nx_channel_error
CALLBACK_test_do_secured_get__server_response_nonce_sync__transaction_endpoint_changed__fails(
    const void* const bytes_to_send,
    unsigned int bytes_count,
    const struct nx_id* const source,
    const struct nx_id* const dest,
    bool is_multicast,
    int NumCalls)
{
    (void) source;
    (void) dest;
    TEST_ASSERT_FALSE(is_multicast);

    // Note: Breakpoint here and examine bytes_to_send and bytes_count
    // to get the 'raw' data which would be sent on the wire

    // interpret the message sent as oc_message_t
    oc_message_t message = {0};
    message.length = bytes_count;
    memcpy(message.data, bytes_to_send, message.length);

    coap_packet_t coap_pkt[1];
    TEST_ASSERT_EQUAL(
        COAP_NO_ERROR,
        coap_udp_parse_message(coap_pkt, message.data, message.length));

    // this is the original outbound client request - a valid message
    if (NumCalls == 0)
    {
        TEST_ASSERT_EQUAL(COAP_TYPE_NON, coap_pkt->type);
        TEST_ASSERT_EQUAL(COAP_GET, coap_pkt->code);

        TEST_ASSERT_EQUAL(APPLICATION_COSE_MAC0, coap_pkt->content_format);
        // 'nx/pc'
        const char* uri_path;
        size_t path_len = coap_get_header_uri_path(coap_pkt, &uri_path);
        TEST_ASSERT_EQUAL(10, path_len);
        TEST_ASSERT_EQUAL_STRING_LEN("nx/fakeuri", uri_path, 10);
        // has COSE MAC0 payload (secured GET message)
        TEST_ASSERT_EQUAL(20, coap_pkt->payload_len);

        // test the encoded payload (is a COSE MAC0 for GET with
        // nonce=4294967265 - nonce NV write interval)
        const uint8_t payload_encoded[20] = {
            0x84, 0x47, 0xA1, 0x05, 0x1A, 0xFF, 0xFF, 0xFF, 0xC1, 0xA0,
            0x40, 0x48, 0x78, 0x27, 0x5f, 0xfe, 0x80, 0x85, 0x70, 0x04};

        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            payload_encoded, coap_pkt->payload, sizeof(payload_encoded));

        // In this test, loop the outbound data back to the same device under
        // test (which is also acting as a server for the secured request)
        // We have to set up another expect here to handle the second send
        // (which will be handled within this stub in the `NumCalls == 1`
        // block below)
        nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);
        nx_channel_network_receive(bytes_to_send, bytes_count, source);
    }
    else if (NumCalls == 1)
    {
        // response being sent back here by the 'server' receiving the
        // previous request will be a 406 nonce sync with a fixed value
        TEST_ASSERT_EQUAL(COAP_TYPE_NON, coap_pkt->type);
        TEST_ASSERT_EQUAL(NOT_ACCEPTABLE_4_06, coap_pkt->code);
        TEST_ASSERT_EQUAL(APPLICATION_COSE_MAC0, coap_pkt->content_format);

        // response has zero URI path length
        const char* uri_path;
        size_t path_len = coap_get_header_uri_path(coap_pkt, &uri_path);
        TEST_ASSERT_EQUAL(0, path_len);
        // has COSE MAC0 payload (secured GET message)
        TEST_ASSERT_EQUAL(20, coap_pkt->payload_len);

        // test the encoded payload (is a CoAP message with response code 406
        // and nonce of 0xFFFFFFFF)
        const uint8_t payload_encoded[37] = {
            0x84, 0x47, 0xa1, 0x05, 0x1a, 0xff, 0xff, 0xff, 0xff, 0xa0,
            0x40, 0x48, 0x91, 0x7c, 0x6d, 0x28, 0x4f, 0x7e, 0xa3, 0x93};

        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            payload_encoded, coap_pkt->payload, sizeof(payload_encoded));

        // arbitrary ID from related test
        struct nx_id linked_id = {0};
        linked_id.authority_id = 53932;
        linked_id.device_id = 4244308258;
        union nexus_channel_link_security_data sec_data;

        bool sec_data_exists =
            nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                               &sec_data.mode0);

        TEST_ASSERT_TRUE(sec_data_exists);
        // because server and client are the same device here, the server
        // sending the nonce sync has already reset the link nonce to 0.
        TEST_ASSERT_EQUAL(0, sec_data.mode0.nonce);

        // clear out the endpoint of the buffered request
        coap_transaction_t* buffered_t =
            coap_get_transaction_by_mid(coap_pkt->mid);
        TEST_ASSERT_NOT_EQUAL(buffered_t, NULL);
        memset(&buffered_t->message->endpoint, 0x00, sizeof(oc_endpoint_t));

        // will not send any response
        nx_channel_network_receive(bytes_to_send, bytes_count, source);
    }
    else
    {
        // no other calls
        TEST_ASSERT_FALSE(true);
    }

    return NX_CHANNEL_ERROR_NONE;
}

// Simulate a linked accessory (linked to itself as a server) which sends
// a secured GET request (as a client) with a valid nonce to a test resource.
//
// However, before the 'server' hosting the test resource receives the request,
// we modify the server state or the request contents in some way to cause
// it to be invalid.
// Test ensures this is handled gracefully.
void test_do_secured_get__server_response_nonce_sync__transaction_endpoint_changed__fails(
    void)
{
    // initializes with no links present
    struct nx_id linked_id = {0};
    linked_id.authority_id = 53932;
    linked_id.device_id = 4244308258;

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    // not testing where this is called in this test
    nxp_common_request_processing_Ignore();

    union nexus_channel_link_security_data sec_data = {
        0}; // nonce and sym key are set explicitly below

    // set the local nonce to 'a high value' nearing rollover.
    sec_data.mode0.nonce =
        UINT32_MAX -
        NEXUS_CHANNEL_LINK_SECURITY_NONCE_NV_STORAGE_INTERVAL_COUNT + 1;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    // this device will be linked to
    nexus_channel_link_manager_create_link(
        &linked_id,
        CHANNEL_LINK_OPERATING_MODE_ACCESSORY,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nexus_channel_core_process(0);
    TEST_ASSERT_EQUAL(0, oc_process_nevents());

    // serve a fake resource we will attempt to get
    // GET
    const struct nx_channel_resource_props fake_res_props = {
        .uri = "nx/fakeuri",
        .resource_type = "angaza.com.nexus.fake_resource",
        .rtr = 65001,
        .num_interfaces = 2,
        .if_masks = if_mask_arr,
        // *REQUEST* handler
        .get_handler = nexus_channel_test_fake_resource_get_handler_valid,
        .get_secured = true, // we are testing the secured GET handler behavior
        .post_handler = NULL,
        .post_secured = false};

    nx_channel_error reg_result = nx_channel_register_resource(&fake_res_props);
    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_NONE, reg_result);
    TEST_ASSERT_EQUAL(0, oc_process_nevents());

    // Now, simulate sending a secured request from this linked accessory to the
    // controller to the secured resource/method (GET /nx/pc)
    nxp_channel_get_nexus_id_IgnoreAndReturn(linked_id);
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);

    nx_channel_error result = nx_channel_do_get_request_secured(
        "nx/fakeuri",
        &linked_id,
        NULL,
        // *RESPONSE* handler, we will eventually call this - once the
        // nonce sync completes and we retry (automatically) with a new nonce.
        &_get_nx_pc_response_handler_should_not_be_called,
        NULL);
    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_NONE, result);
    // one event for OUTBOUND_NETWORK_EVENT, + 1 for `poll_requested` flag
    TEST_ASSERT_EQUAL(2, oc_process_nevents());

    // will be called as a result of `oc_do_get`
    // This callback is configured to check the outbound data for expected
    // values, then 'receive' the data
    nxp_channel_network_send_Stub(
        CALLBACK_test_do_secured_get__server_response_nonce_sync__transaction_endpoint_changed__fails);

    // Still waiting to send outbound message
    TEST_ASSERT_EQUAL(2, oc_process_nevents());
    nexus_channel_core_process(0);

    // ensure there are no pending processes, and all message buffers are empty
    TEST_ASSERT_EQUAL(0, oc_process_nevents());
}

nx_channel_error
CALLBACK_test_do_secured_get__server_response_nonce_sync__transaction_deleted_before_resend_occurs__fails(
    const void* const bytes_to_send,
    unsigned int bytes_count,
    const struct nx_id* const source,
    const struct nx_id* const dest,
    bool is_multicast,
    int NumCalls)
{
    (void) source;
    (void) dest;
    TEST_ASSERT_FALSE(is_multicast);

    // Note: Breakpoint here and examine bytes_to_send and bytes_count
    // to get the 'raw' data which would be sent on the wire

    // interpret the message sent as oc_message_t
    oc_message_t message = {0};
    message.length = bytes_count;
    memcpy(message.data, bytes_to_send, message.length);

    coap_packet_t coap_pkt[1];
    TEST_ASSERT_EQUAL(
        COAP_NO_ERROR,
        coap_udp_parse_message(coap_pkt, message.data, message.length));

    // this is the original outbound client request - a valid message
    if (NumCalls == 0)
    {
        TEST_ASSERT_EQUAL(COAP_TYPE_NON, coap_pkt->type);
        TEST_ASSERT_EQUAL(COAP_GET, coap_pkt->code);

        TEST_ASSERT_EQUAL(APPLICATION_COSE_MAC0, coap_pkt->content_format);
        // 'nx/pc'
        const char* uri_path;
        size_t path_len = coap_get_header_uri_path(coap_pkt, &uri_path);
        TEST_ASSERT_EQUAL(10, path_len);
        TEST_ASSERT_EQUAL_STRING_LEN("nx/fakeuri", uri_path, 10);
        // has COSE MAC0 payload (secured GET message)
        TEST_ASSERT_EQUAL(20, coap_pkt->payload_len);

        // test the encoded payload (is a COSE MAC0 for GET with
        // nonce=4294967265 - nonce NV write interval)
        const uint8_t payload_encoded[20] = {
            0x84, 0x47, 0xA1, 0x05, 0x1A, 0xFF, 0xFF, 0xFF, 0xC1, 0xA0,
            0x40, 0x48, 0x78, 0x27, 0x5f, 0xfe, 0x80, 0x85, 0x70, 0x04};

        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            payload_encoded, coap_pkt->payload, sizeof(payload_encoded));

        // In this test, loop the outbound data back to the same device under
        // test (which is also acting as a server for the secured request)
        // We have to set up another expect here to handle the second send
        // (which will be handled within this stub in the `NumCalls == 1`
        // block below)
        nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);
        nx_channel_network_receive(bytes_to_send, bytes_count, source);
    }
    else if (NumCalls == 1)
    {
        // response being sent back here by the 'server' receiving the
        // previous request will be a 406 nonce sync with a fixed value
        TEST_ASSERT_EQUAL(COAP_TYPE_NON, coap_pkt->type);
        TEST_ASSERT_EQUAL(NOT_ACCEPTABLE_4_06, coap_pkt->code);
        TEST_ASSERT_EQUAL(APPLICATION_COSE_MAC0, coap_pkt->content_format);

        // response has zero URI path length
        const char* uri_path;
        size_t path_len = coap_get_header_uri_path(coap_pkt, &uri_path);
        TEST_ASSERT_EQUAL(0, path_len);
        // has COSE MAC0 payload (secured GET message)
        TEST_ASSERT_EQUAL(20, coap_pkt->payload_len);

        // test the encoded payload (is a CoAP message with response code 406
        // and nonce of 0xFFFFFFFF)
        const uint8_t payload_encoded[37] = {
            0x84, 0x47, 0xa1, 0x05, 0x1a, 0xff, 0xff, 0xff, 0xff, 0xa0,
            0x40, 0x48, 0x91, 0x7c, 0x6d, 0x28, 0x4f, 0x7e, 0xa3, 0x93};

        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            payload_encoded, coap_pkt->payload, sizeof(payload_encoded));

        // arbitrary ID from related test
        struct nx_id linked_id = {0};
        linked_id.authority_id = 53932;
        linked_id.device_id = 4244308258;
        union nexus_channel_link_security_data sec_data;

        bool sec_data_exists =
            nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                               &sec_data.mode0);

        TEST_ASSERT_TRUE(sec_data_exists);
        // because server and client are the same device here, the server
        // sending the nonce sync has already reset the link nonce to 0.
        TEST_ASSERT_EQUAL(0, sec_data.mode0.nonce);

        // clear all transactions, causing us to lose the buffered message to
        // resend.
        coap_free_all_transactions();
        // will not send any response
        nx_channel_network_receive(bytes_to_send, bytes_count, source);
    }
    else
    {
        // no other calls
        TEST_ASSERT_FALSE(true);
    }

    return NX_CHANNEL_ERROR_NONE;
}

// Simulate a linked accessory (linked to itself as a server) which sends
// a secured GET request (as a client) with a valid nonce to a test resource.
//
// However, before the 'server' hosting the test resource receives the request,
// we modify the server state or the request contents in some way to cause
// it to be invalid.
// Test ensures this is handled gracefully.
void test_do_secured_get__server_response_nonce_sync__transaction_deleted_before_resend_occurs__fails(
    void)
{
    // initializes with no links present
    struct nx_id linked_id = {0};
    linked_id.authority_id = 53932;
    linked_id.device_id = 4244308258;

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    // not testing where this is called in this test
    nxp_common_request_processing_Ignore();

    union nexus_channel_link_security_data sec_data = {
        0}; // nonce and sym key are set explicitly below

    // set the local nonce to 'a high value' nearing rollover.
    sec_data.mode0.nonce =
        UINT32_MAX -
        NEXUS_CHANNEL_LINK_SECURITY_NONCE_NV_STORAGE_INTERVAL_COUNT + 1;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    // this device will be linked to
    nexus_channel_link_manager_create_link(
        &linked_id,
        CHANNEL_LINK_OPERATING_MODE_ACCESSORY,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nexus_channel_core_process(0);
    TEST_ASSERT_EQUAL(0, oc_process_nevents());

    // serve a fake resource we will attempt to get
    // GET
    const struct nx_channel_resource_props fake_res_props = {
        .uri = "nx/fakeuri",
        .resource_type = "angaza.com.nexus.fake_resource",
        .rtr = 65001,
        .num_interfaces = 2,
        .if_masks = if_mask_arr,
        // *REQUEST* handler
        .get_handler = nexus_channel_test_fake_resource_get_handler_valid,
        .get_secured = true, // we are testing the secured GET handler behavior
        .post_handler = NULL,
        .post_secured = false};

    nx_channel_error reg_result = nx_channel_register_resource(&fake_res_props);
    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_NONE, reg_result);
    TEST_ASSERT_EQUAL(0, oc_process_nevents());

    // Now, simulate sending a secured request from this linked accessory to the
    // controller to the secured resource/method (GET /nx/pc)
    nxp_channel_get_nexus_id_IgnoreAndReturn(linked_id);
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);

    nx_channel_error result = nx_channel_do_get_request_secured(
        "nx/fakeuri",
        &linked_id,
        NULL,
        // *RESPONSE* handler, we will eventually call this - once the
        // nonce sync completes and we retry (automatically) with a new nonce.
        &_get_nx_pc_response_handler_should_not_be_called,
        NULL);
    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_NONE, result);
    // one event for OUTBOUND_NETWORK_EVENT, + 1 for `poll_requested` flag
    TEST_ASSERT_EQUAL(2, oc_process_nevents());

    // will be called as a result of `oc_do_get`
    // This callback is configured to check the outbound data for expected
    // values, then 'receive' the data
    nxp_channel_network_send_Stub(
        CALLBACK_test_do_secured_get__server_response_nonce_sync__transaction_deleted_before_resend_occurs__fails);

    // Still waiting to send outbound message
    TEST_ASSERT_EQUAL(2, oc_process_nevents());
    nexus_channel_core_process(0);

    // ensure there are no pending processes, and all message buffers are empty
    TEST_ASSERT_EQUAL(0, oc_process_nevents());
}

nx_channel_error
CALLBACK_test_do_secured_get__server_response_nonce_sync__link_lost_before_attempting_resend__ok(
    const void* const bytes_to_send,
    unsigned int bytes_count,
    const struct nx_id* const source,
    const struct nx_id* const dest,
    bool is_multicast,
    int NumCalls)
{
    (void) source;
    (void) dest;
    TEST_ASSERT_FALSE(is_multicast);

    // Note: Breakpoint here and examine bytes_to_send and bytes_count
    // to get the 'raw' data which would be sent on the wire

    // interpret the message sent as oc_message_t
    oc_message_t message = {0};
    message.length = bytes_count;
    memcpy(message.data, bytes_to_send, message.length);

    coap_packet_t coap_pkt[1];
    TEST_ASSERT_EQUAL(
        COAP_NO_ERROR,
        coap_udp_parse_message(coap_pkt, message.data, message.length));

    // this is the original outbound client request - a valid message
    if (NumCalls == 0)
    {
        TEST_ASSERT_EQUAL(COAP_TYPE_NON, coap_pkt->type);
        TEST_ASSERT_EQUAL(COAP_GET, coap_pkt->code);

        TEST_ASSERT_EQUAL(APPLICATION_COSE_MAC0, coap_pkt->content_format);
        // 'nx/pc'
        const char* uri_path;
        size_t path_len = coap_get_header_uri_path(coap_pkt, &uri_path);
        TEST_ASSERT_EQUAL(10, path_len);
        TEST_ASSERT_EQUAL_STRING_LEN("nx/fakeuri", uri_path, 10);
        // has COSE MAC0 payload (secured GET message)
        TEST_ASSERT_EQUAL(20, coap_pkt->payload_len);

        // test the encoded payload (is a COSE MAC0 for GET with
        // nonce=4294967265 - nonce NV write interval)
        const uint8_t payload_encoded[20] = {
            0x84, 0x47, 0xA1, 0x05, 0x1A, 0xFF, 0xFF, 0xFF, 0xC1, 0xA0,
            0x40, 0x48, 0x78, 0x27, 0x5f, 0xfe, 0x80, 0x85, 0x70, 0x04};

        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            payload_encoded, coap_pkt->payload, sizeof(payload_encoded));

        // In this test, loop the outbound data back to the same device under
        // test (which is also acting as a server for the secured request)
        // We have to set up another expect here to handle the second send
        // (which will be handled within this stub in the `NumCalls == 1`
        // block below)
        nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);
        nx_channel_network_receive(bytes_to_send, bytes_count, source);
    }
    else if (NumCalls == 1)
    {
        // response being sent back here by the 'server' receiving the
        // previous request will be a 406 nonce sync with a fixed value
        TEST_ASSERT_EQUAL(COAP_TYPE_NON, coap_pkt->type);
        TEST_ASSERT_EQUAL(NOT_ACCEPTABLE_4_06, coap_pkt->code);
        TEST_ASSERT_EQUAL(APPLICATION_COSE_MAC0, coap_pkt->content_format);

        // response has zero URI path length
        const char* uri_path;
        size_t path_len = coap_get_header_uri_path(coap_pkt, &uri_path);
        TEST_ASSERT_EQUAL(0, path_len);
        // has COSE MAC0 payload (secured GET message)
        TEST_ASSERT_EQUAL(20, coap_pkt->payload_len);

        // test the encoded payload (is a CoAP message with response code 406
        // and nonce of 0xFFFFFFFF)
        const uint8_t payload_encoded[37] = {
            0x84, 0x47, 0xa1, 0x05, 0x1a, 0xff, 0xff, 0xff, 0xff, 0xa0,
            0x40, 0x48, 0x91, 0x7c, 0x6d, 0x28, 0x4f, 0x7e, 0xa3, 0x93};

        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            payload_encoded, coap_pkt->payload, sizeof(payload_encoded));

        // arbitrary ID from related test
        struct nx_id linked_id = {0};
        linked_id.authority_id = 53932;
        linked_id.device_id = 4244308258;
        union nexus_channel_link_security_data sec_data;

        bool sec_data_exists =
            nexus_channel_link_manager_security_data_from_nxid(&linked_id,
                                                               &sec_data.mode0);

        TEST_ASSERT_TRUE(sec_data_exists);
        // because server and client are the same device here, the server
        // sending the nonce sync has already reset the link nonce to 0.
        TEST_ASSERT_EQUAL(0, sec_data.mode0.nonce);

        // here, delete the nexus channel link before we are able to resend.
        // nonce sync 'reset'.
        // Lets set our local nonce to something not-zero, and ensure that
        // we reset it to 0 after receiving this message
        nexus_channel_link_manager_clear_all_links();
        nexus_channel_link_manager_process(0);
        // Should elicit an error response
        nx_channel_network_receive(bytes_to_send, bytes_count, source);
    }
    else if (NumCalls == 2)
    {
        // response to NumCalls == 1, error message
        TEST_ASSERT_EQUAL(5, bytes_count);

        const uint8_t expected_bytes[5] = {0x51, 0x81, 0xe2, 0x41, 0x40};

        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            bytes_to_send, expected_bytes, sizeof(expected_bytes));
        nexus_channel_core_process(1);
    }
    else
    {
        // no other calls
        TEST_ASSERT_FALSE(true);
    }

    return NX_CHANNEL_ERROR_NONE;
}

// Simulate a linked accessory (linked to itself as a server) which sends
// a secured GET request (as a client) with a valid nonce to a test resource.
//
// However, before the 'server' hosting the test resource receives the request,
// we modify the server state or the request contents in some way to cause
// it to be invalid.
// Test ensures this is handled gracefully.
void test_do_secured_get__server_response_nonce_sync__link_lost_before_attempting_resend__ok(
    void)
{
    // initializes with no links present
    struct nx_id linked_id = {0};
    linked_id.authority_id = 53932;
    linked_id.device_id = 4244308258;

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    // not testing where this is called in this test
    nxp_common_request_processing_Ignore();

    union nexus_channel_link_security_data sec_data = {
        0}; // nonce and sym key are set explicitly below

    // set the local nonce to 'a high value' nearing rollover.
    sec_data.mode0.nonce =
        UINT32_MAX -
        NEXUS_CHANNEL_LINK_SECURITY_NONCE_NV_STORAGE_INTERVAL_COUNT + 1;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    // this device will be linked to
    nexus_channel_link_manager_create_link(
        &linked_id,
        CHANNEL_LINK_OPERATING_MODE_ACCESSORY,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nexus_channel_core_process(0);
    TEST_ASSERT_EQUAL(0, oc_process_nevents());

    // serve a fake resource we will attempt to get
    // GET
    const struct nx_channel_resource_props fake_res_props = {
        .uri = "nx/fakeuri",
        .resource_type = "angaza.com.nexus.fake_resource",
        .rtr = 65001,
        .num_interfaces = 2,
        .if_masks = if_mask_arr,
        // *REQUEST* handler
        .get_handler = nexus_channel_test_fake_resource_get_handler_valid,
        .get_secured = true, // we are testing the secured GET handler behavior
        .post_handler = NULL,
        .post_secured = false};

    nx_channel_error reg_result = nx_channel_register_resource(&fake_res_props);
    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_NONE, reg_result);
    TEST_ASSERT_EQUAL(0, oc_process_nevents());

    // Now, simulate sending a secured request from this linked accessory to the
    // controller to the secured resource/method (GET /nx/pc)
    nxp_channel_get_nexus_id_IgnoreAndReturn(linked_id);
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);

    nx_channel_error result = nx_channel_do_get_request_secured(
        "nx/fakeuri",
        &linked_id,
        NULL,
        // *RESPONSE* handler, we will eventually call this - once the
        // nonce sync completes and we retry (automatically) with a new nonce.
        &_get_nx_pc_response_handler_should_not_be_called,
        NULL);
    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_NONE, result);
    // one event for OUTBOUND_NETWORK_EVENT, + 1 for `poll_requested` flag
    TEST_ASSERT_EQUAL(2, oc_process_nevents());

    // will be called as a result of `oc_do_get`
    // This callback is configured to check the outbound data for expected
    // values, then 'receive' the data
    nxp_channel_network_send_Stub(
        CALLBACK_test_do_secured_get__server_response_nonce_sync__link_lost_before_attempting_resend__ok);

    // Still waiting to send outbound message
    TEST_ASSERT_EQUAL(2, oc_process_nevents());
    nexus_channel_core_process(0);

    // ensure there are no pending processes, and all message buffers are empty
    TEST_ASSERT_EQUAL(0, oc_process_nevents());

    // link was deleted as part of mocked network_send stub
    TEST_ASSERT_FALSE(nexus_channel_link_manager_security_data_from_nxid(
        &linked_id, &sec_data.mode0));
}

/* Simulate a number of secured GET requests, which should be cached in a local
 * buffer. We should hit a limit (ultimately driven by
 * `OC_MAX_NUM_CONCURRENT_REQUESTS`) after which we cannot send more secured
 * messages until the buffered ones clear out (after a timeout elapses).
 */
void test_do_secured_get__multiple_requests_consuming_transaction_buffer__buffer_frees_after_timeout(
    void)
{
    // initializes with no links present
    struct nx_id linked_id = {0};
    linked_id.authority_id = 53932;
    linked_id.device_id = 4244308258;

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    // not testing where this is called in this test
    nxp_common_request_processing_Ignore();

    union nexus_channel_link_security_data sec_data = {
        0}; // nonce and sym key are set explicitly below

    // set the local nonce to 'a high value' nearing rollover.
    sec_data.mode0.nonce =
        UINT32_MAX -
        NEXUS_CHANNEL_LINK_SECURITY_NONCE_NV_STORAGE_INTERVAL_COUNT + 1;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    // this device will be linked to
    nexus_channel_link_manager_create_link(
        &linked_id,
        CHANNEL_LINK_OPERATING_MODE_ACCESSORY,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nexus_channel_core_process(0);
    TEST_ASSERT_EQUAL(0, oc_process_nevents());

    // Now, simulate sending a secured request from this linked accessory to
    // itself, which will essentially be ignored
    // controller to the secured resource/method
    nxp_channel_get_nexus_id_IgnoreAndReturn(linked_id);

    nx_channel_error result = nx_channel_do_get_request_secured(
        "nx/fakeuri",
        &linked_id,
        NULL,
        // response handler won't be called, request is never received
        &_get_nx_pc_response_handler_should_not_be_called,
        NULL);
    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_NONE, result);
    // one event for OUTBOUND_NETWORK_EVENT, + 1 for `poll_requested` flag
    TEST_ASSERT_EQUAL(2, oc_process_nevents());

    // will be called as a result of `oc_do_get`. We ignore the message,
    // so no response will be generated.
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);

    // Still waiting to send outbound message
    TEST_ASSERT_EQUAL(2, oc_process_nevents());
    nexus_channel_core_process(0);

    // ensure there are no pending processes
    TEST_ASSERT_EQUAL(0, oc_process_nevents());
    // transactions
    TEST_ASSERT_EQUAL(COAP_MAX_OPEN_TRANSACTIONS - 1,
                      coap_transactions_free_count());
    // client callbacks (cbs allow 1 + OC_MAX_NUM_CONCURRENT_REQUESTS)
    TEST_ASSERT_EQUAL(OC_MAX_NUM_CONCURRENT_REQUESTS,
                      oc_ri_client_cb_free_count());
    // oc_message incoming/outgoing buffers
    TEST_ASSERT_EQUAL(OC_MAX_NUM_CONCURRENT_REQUESTS - 1,
                      oc_buffer_outgoing_free_count());

    // 1 incoming buffer consumed within test setup function...
    TEST_ASSERT_EQUAL(OC_MAX_NUM_CONCURRENT_REQUESTS - 1,
                      oc_buffer_incoming_free_count());

    // send another GET request
    result = nx_channel_do_get_request_secured(
        "nx/fakeuri",
        &linked_id,
        NULL,
        // response handler won't be called, request is never received
        &_get_nx_pc_response_handler_should_not_be_called,
        NULL);
    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_NONE, result);

    // will be called as a result of `oc_do_get`. We ignore the message,
    // so no response will be generated.
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);

    // one event for OUTBOUND_NETWORK_EVENT, + 1 for `poll_requested` flag
    TEST_ASSERT_EQUAL(2, oc_process_nevents());
    nexus_channel_core_process(0);

    // ensure there are no pending processes
    TEST_ASSERT_EQUAL(0, oc_process_nevents());

    // transactions
    TEST_ASSERT_EQUAL(COAP_MAX_OPEN_TRANSACTIONS - 2,
                      coap_transactions_free_count());
    // client callbacks (cbs allow 1 + OC_MAX_NUM_CONCURRENT_REQUESTS)
    TEST_ASSERT_EQUAL(OC_MAX_NUM_CONCURRENT_REQUESTS - 1,
                      oc_ri_client_cb_free_count());
    // oc_message incoming/outgoing buffers
    TEST_ASSERT_EQUAL(OC_MAX_NUM_CONCURRENT_REQUESTS - 2,
                      oc_buffer_outgoing_free_count());

    // 1 incoming buffer consumed within test setup function...
    TEST_ASSERT_EQUAL(OC_MAX_NUM_CONCURRENT_REQUESTS - 1,
                      oc_buffer_incoming_free_count());

    // we've used the max - no more transactions are possible. Will fail
    // another GET request.

    // send another GET request
    result = nx_channel_do_get_request_secured(
        "nx/fakeuri",
        &linked_id,
        NULL,
        // response handler won't be called, request is never received
        &_get_nx_pc_response_handler_should_not_be_called,
        NULL);
    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_UNSPECIFIED, result);

    // no pending processes - we didn't trigger any outbound message
    TEST_ASSERT_EQUAL(0, oc_process_nevents());
    nexus_channel_core_process(0);
    TEST_ASSERT_EQUAL(0, oc_process_nevents());

    // allow time to elapse - hardcoded to exceed transaction cache timeout
    // of 5 seconds (by passing 6 seconds as elapsed here).
    // `nx_common_process` is the interface to update Nexus 'uptime' value.
    nexus_keycode_core_process_IgnoreAndReturn(0);
    (void) nx_common_process(6);

    // now, the buffered transactions should be cleared out so we can
    // make another secured request.
    result = nx_channel_do_get_request_secured(
        "nx/fakeuri",
        &linked_id,
        NULL,
        // response handler won't be called, request is never received
        &_get_nx_pc_response_handler_should_not_be_called,
        NULL);
    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_NONE, result);
    // one event for OUTBOUND_NETWORK_EVENT, + 1 for `poll_requested` flag
    TEST_ASSERT_EQUAL(2, oc_process_nevents());

    // will be called as a result of `oc_do_get`. We ignore the message,
    // so no response will be generated.
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);

    // Still waiting to send outbound message
    TEST_ASSERT_EQUAL(2, oc_process_nevents());
    nexus_channel_core_process(0);

    // ensure there are no pending processes
    TEST_ASSERT_EQUAL(0, oc_process_nevents());
    // transactions
    TEST_ASSERT_EQUAL(COAP_MAX_OPEN_TRANSACTIONS - 1,
                      coap_transactions_free_count());
    // client callbacks (cbs allow 1 + OC_MAX_NUM_CONCURRENT_REQUESTS)
    TEST_ASSERT_EQUAL(OC_MAX_NUM_CONCURRENT_REQUESTS,
                      oc_ri_client_cb_free_count());
    // oc_message incoming/outgoing buffers
    TEST_ASSERT_EQUAL(OC_MAX_NUM_CONCURRENT_REQUESTS - 1,
                      oc_buffer_outgoing_free_count());

    TEST_ASSERT_EQUAL(OC_MAX_NUM_CONCURRENT_REQUESTS - 1,
                      oc_buffer_incoming_free_count());
}

nx_channel_error
CALLBACK_test_do_secured_get_to_unsecured_resource__unsecured_reply_ignored(
    const void* const bytes_to_send,
    unsigned int bytes_count,
    const struct nx_id* const source,
    const struct nx_id* const dest,
    bool is_multicast,
    int NumCalls)
{
    (void) source;
    (void) dest;
    TEST_ASSERT_FALSE(is_multicast);

    // Note: Breakpoint here and examine bytes_to_send and bytes_count
    // to get the 'raw' data which would be sent on the wire

    // interpret the message sent as oc_message_t
    oc_message_t message = {0};
    message.length = bytes_count;
    memcpy(message.data, bytes_to_send, message.length);

    coap_packet_t coap_pkt[1];
    TEST_ASSERT_EQUAL(
        COAP_NO_ERROR,
        coap_udp_parse_message(coap_pkt, message.data, message.length));

    // this is the original outbound client request - a valid message
    if (NumCalls == 0)
    {
        TEST_ASSERT_EQUAL(COAP_TYPE_NON, coap_pkt->type);
        TEST_ASSERT_EQUAL(COAP_GET, coap_pkt->code);

        TEST_ASSERT_EQUAL(APPLICATION_COSE_MAC0, coap_pkt->content_format);
        const char* uri_path;
        size_t path_len = coap_get_header_uri_path(coap_pkt, &uri_path);
        TEST_ASSERT_EQUAL(10, path_len);
        TEST_ASSERT_EQUAL_STRING_LEN("nx/fakeuri", uri_path, 10);
        // has COSE MAC0 payload (secured GET message)
        TEST_ASSERT_EQUAL(16, coap_pkt->payload_len);

        // test the encoded payload (is a COSE MAC0 for GET with
        // nonce=5)
        const uint8_t payload_encoded[16] = {0x84,
                                             0x43,
                                             0xA1,
                                             0x05,
                                             0x06,
                                             0xA0,
                                             0x40,
                                             0x48,
                                             0x2D,
                                             0x9D,
                                             0x38,
                                             0x15,
                                             0x7F,
                                             0xAC,
                                             0xA1,
                                             0xF9};

        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            payload_encoded, coap_pkt->payload, sizeof(payload_encoded));

        // simulated reply message from MITM; all correct except that it is
        // unsecured
        coap_packet_t mitm_packet;
        // initialize packet: PUT with arbitrary message ID
        coap_udp_init_message(
            &mitm_packet, COAP_TYPE_CON, CONTENT_2_05, coap_pkt->mid);
        // set the request URI path and content format
        coap_set_header_uri_path(
            &mitm_packet, "nx/fakeuri", strlen("nx/fakeuri"));
        coap_set_header_content_format(&mitm_packet, APPLICATION_VND_OCF_CBOR);
        coap_set_token(&mitm_packet, coap_pkt->token, coap_pkt->token_len);

        // HELLO WORLD
        uint8_t unsecured_payload[11] = {
            0x48, 0x45, 0x4C, 0x4C, 0x4F, 0x20, 0x57, 0x4F, 0x52, 0x4C, 0x44};

        coap_set_payload(
            &mitm_packet, unsecured_payload, sizeof(unsecured_payload));

        uint8_t out_buffer[NEXUS_CHANNEL_MAX_COAP_TOTAL_MESSAGE_SIZE];
        size_t out_length =
            coap_serialize_message((void*) &mitm_packet, out_buffer);

        nx_channel_network_receive((void*) out_buffer, out_length, source);
    }
    else
    {
        // no other calls
        TEST_ASSERT_FALSE(true);
    }

    return NX_CHANNEL_ERROR_NONE;
}

// Simulate a linked accessory (linked to itself as a server) which sends
// a secured GET request (as a client) with a valid nonce to an *unsecured*
// resource.
//
// We then simulate a MITM attack by sending back an *unsecured* reply to this
// request and ensure that the security layer does not allow the client
// callback function to be invoked.
void test_do_secured_get_to_unsecured_resource__unsecured_reply_ignored__ok(
    void)
{
    // initializes with no links present
    struct nx_id linked_id = {0};
    linked_id.authority_id = 53932;
    linked_id.device_id = 4244308258;

    struct nx_common_check_key link_key;
    memset(&link_key, 0xFA, sizeof(link_key)); // arbitrary

    // not testing where this is called in this test
    nxp_common_request_processing_Ignore();

    union nexus_channel_link_security_data sec_data = {
        0}; // nonce and sym key are set explicitly below

    // set the local nonce to 'a high value' nearing rollover.
    sec_data.mode0.nonce = 5;
    memcpy(
        &sec_data.mode0.sym_key, &link_key, sizeof(struct nx_common_check_key));

    // link the device
    nexus_channel_link_manager_create_link(
        &linked_id,
        CHANNEL_LINK_OPERATING_MODE_ACCESSORY,
        NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24,
        &sec_data);
    nexus_channel_core_process(0);
    TEST_ASSERT_EQUAL(0, oc_process_nevents());

    // serve a fake resource we will attempt to GET
    const struct nx_channel_resource_props fake_res_props = {
        .uri = "nx/fakeuri",
        .resource_type = "angaza.com.nexus.fake_resource",
        .rtr = 65001,
        .num_interfaces = 2,
        .if_masks = if_mask_arr,
        // *REQUEST* handler
        .get_handler = nexus_channel_test_fake_resource_get_handler_valid,
        .get_secured = false, // the resource is UNSECURED
        .post_handler = NULL,
        .post_secured = false};

    nx_channel_error reg_result = nx_channel_register_resource(&fake_res_props);
    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_NONE, reg_result);
    TEST_ASSERT_EQUAL(0, oc_process_nevents());

    // Now, simulate sending a secured request from this linked accessory to the
    // controller to the secured resource/method
    nxp_channel_get_nexus_id_IgnoreAndReturn(linked_id);
    nxp_channel_network_send_ExpectAnyArgsAndReturn(NX_CHANNEL_ERROR_NONE);

    nx_channel_error result = nx_channel_do_get_request_secured(
        "nx/fakeuri",
        &linked_id,
        NULL,
        // *RESPONSE* handler, should NOT be called in this test
        &_get_nx_pc_response_handler_should_not_be_called,
        NULL);
    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_NONE, result);
    // one event for OUTBOUND_NETWORK_EVENT, + 1 for `poll_requested` flag
    TEST_ASSERT_EQUAL(2, oc_process_nevents());

    // will be called as a result of `oc_do_get`
    // This callback is configured to check the outbound data for expected
    // values, then 'receive' the data
    nxp_channel_network_send_Stub(
        CALLBACK_test_do_secured_get_to_unsecured_resource__unsecured_reply_ignored);

    // Still waiting to send outbound message
    TEST_ASSERT_EQUAL(2, oc_process_nevents());
    nexus_channel_core_process(0);

    // ensure there are no pending processes, and all message buffers are empty
    TEST_ASSERT_EQUAL(0, oc_process_nevents());
}

#pragma GCC diagnostic pop
