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
#include "oc/util/oc_etimer.h"
#include "oc/util/oc_memb.h"
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
#include "src/nexus_channel_sm.h"
#include "src/nexus_common_internal.h"
#include "src/nexus_cose_mac0_common.h"
#include "src/nexus_cose_mac0_sign.h"
#include "src/nexus_cose_mac0_verify.h"
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
#include <mock_nexus_channel_res_payg_credit.h>
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

/********************************************************
 * PRIVATE TYPES
 *******************************************************/

/********************************************************
 * PRIVATE DATA
 *******************************************************/
// Used for buffering responses
static uint8_t RESP_BUFFER[2048];
static const oc_interface_mask_t if_mask_arr[] = {OC_IF_BASELINE, OC_IF_RW};
static coap_packet_t response_packet = {0};
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

// Setup (called before any 'test_*' function is called, automatically)
void setUp(void)
{
    nxp_channel_random_value_IgnoreAndReturn(123456);
    nxp_channel_network_send_IgnoreAndReturn(NX_CHANNEL_ERROR_NONE);
    nxp_common_nv_read_IgnoreAndReturn(true);
    nexus_channel_res_payg_credit_process_IgnoreAndReturn(UINT32_MAX);

    bool init_success = nexus_channel_core_init();
    TEST_ASSERT_TRUE(init_success);

    memset(RESP_BUFFER, 0x00, sizeof(RESP_BUFFER));
    memset(&response_packet, 0x00, sizeof(response_packet));
}

// Teardown (called after any 'test_*' function is called, automatically)
void tearDown(void)
{
    nexus_channel_core_shutdown();
}

void test_channel_common_init__platform_device_registration_ok(void)
{
    oc_platform_info_t* platform_info = oc_core_get_platform_info();
    TEST_ASSERT_EQUAL(0, strcmp("Angaza", platform_info->mfg_name.ptr));

    oc_device_info_t* device_info =
        oc_core_get_device_info(NEXUS_CHANNEL_NEXUS_DEVICE_ID);
    TEST_ASSERT_EQUAL(0, strcmp("Nexus Channel", device_info->name.ptr));
    TEST_ASSERT_EQUAL(0, strcmp("ocf.2.1.1", device_info->icv.ptr));
    TEST_ASSERT_EQUAL(0, strcmp("ocf.res.1.3.0", device_info->dmv.ptr));
}

void test_channel_common_init__add_device__limit_reached_fails(void)
{
    int ret = oc_add_device("/oic/test/",
                            "acme.com.widget",
                            "Acme Widget",
                            "ocf.2.1.1",
                            "ocf.res.1.3.0",
                            NULL,
                            NULL);

    // assumes device limit of 1; composite device model used for
    // new resources added by port-side code
    TEST_ASSERT_TRUE(ret < 0);
}

void test_channel_common_register_resource_and_handler__ok(void)
{
    // register resource
    const struct nx_channel_resource_props pc_props = {
        .uri = "/c",
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
}

void test_channel_common_register_resource_and_multiple_handlers__ok(void)
{
    // register resource
    const struct nx_channel_resource_props pc_props = {
        .uri = "/c",
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

    // ignore that the handlers don't *actually* implement these methods; we
    // just need a function pointer
    reg_result = nx_channel_register_resource_handler(
        "/c", OC_POST, nexus_channel_res_payg_credit_get_handler, false);

    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_NONE, reg_result);

    reg_result = nx_channel_register_resource_handler(
        "/c", OC_PUT, nexus_channel_res_payg_credit_get_handler, false);

    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_METHOD_UNSUPPORTED, reg_result);

    reg_result = nx_channel_register_resource_handler(
        "/c", OC_DELETE, nexus_channel_res_payg_credit_get_handler, false);

    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_METHOD_UNSUPPORTED, reg_result);
}

// Attempt to register a duplicate resource
void test_channel_common_register_resource__uri_exists_fails(void)
{
    // register resource
    const struct nx_channel_resource_props pc_props = {
        .uri = "/c",
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

    // duplicate resource registration attempt should fail
    reg_result = nx_channel_register_resource(&pc_props);

    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_UNSPECIFIED, reg_result);
}

// Attempt to register a duplicate resource handler
void test_channel_common_register_resource_handler__handler_exists_fails(void)
{
    // register resource
    const struct nx_channel_resource_props pc_props = {
        .uri = "/c",
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

    nx_channel_error handler_reg_result = nx_channel_register_resource_handler(
        "/c", OC_GET, nexus_channel_res_payg_credit_get_handler, false);

    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_ACTION_REJECTED, handler_reg_result);
}

// Attempt to register secured resources when security manager is unable
// to register a new resource method
void test_channel_core_register_resource_handler__too_many_secured_methods__fails(
    void)
{
    // register resource
    struct nx_channel_resource_props res_props = {
        //
        .uri = "", // will be overwritten
        .resource_type = "", // will be overwritten
        .rtr = 65000,
        .num_interfaces = 2,
        .if_masks = if_mask_arr,
        // handlers are dummy... not actually called in this test
        .get_handler = nexus_channel_res_payg_credit_get_handler,
        .get_secured = true,
        .post_handler = nexus_channel_res_payg_credit_post_handler,
        .post_secured = true};

    // fill the `nexus_sec_res_methods` member so we can't register new
    // resources
    char uri[10] = "/";
    char res_type[100] = "x.com.dummy.resource";
    char dummy_char;

    for (int i = 0; i < OC_MAX_APP_RESOURCES - 1; i++)
    {
        // arbitrary 1-digit URI
        dummy_char = (char) (0x30 + i);
        uri[1] = dummy_char;
        res_type[0] = dummy_char;
        res_props.uri = uri;
        res_props.resource_type = res_type;
        res_props.rtr++;
        nx_channel_error reg_result = nx_channel_register_resource(&res_props);
        TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_NONE, reg_result);
    }

    nx_channel_error reg_result = nx_channel_register_resource(&res_props);
    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_UNSPECIFIED, reg_result);
}

// Attempt to register a handler to a resource that doesn't exist
void test_channel_common_register_resource_handler__resource_undefined_fails(
    void)
{
    nx_channel_error handler_reg_result = nx_channel_register_resource_handler(
        "/c", OC_GET, nexus_channel_res_payg_credit_get_handler, false);

    TEST_ASSERT_EQUAL(NX_CHANNEL_ERROR_UNSPECIFIED, handler_reg_result);
}

void test_channel_common_input_coap_message_passed_to_registered_handler__ok(
    void)
{
    // register platform and device
    nexus_channel_core_init();

    // register resource
    const struct nx_channel_resource_props pc_props = {
        .uri = "/c",
        .resource_type = "angaza.com.nexus.payg_credit",
        .rtr = 65000,
        .num_interfaces = 2,
        .if_masks = if_mask_arr,
        .get_handler = nexus_channel_res_payg_credit_get_handler,
        .get_secured = false,
        .post_handler = NULL,
        .post_secured = false};

    nx_channel_error reg_result = nx_channel_register_resource(&pc_props);

    TEST_ASSERT_EQUAL(reg_result, 0);
    coap_packet_t request_packet;
    // initialize packet: confirmable message (GET) with arbitrary message ID
    coap_udp_init_message(&request_packet, COAP_TYPE_CON, 1, 123);
    // set the request URI path
    coap_set_header_uri_path(&request_packet, "/c", strlen("/c"));

    oc_message_t* request_message = oc_allocate_message();
    if (request_message)
    {
        // set the message length based on the result of data serialization
        request_message->length =
            coap_serialize_message(&request_packet, request_message->data);
    }

    // pass the request message to the CoAP parser, which should route to the
    // correct handler
    nexus_channel_res_payg_credit_get_handler_ExpectAnyArgs();
    int result = coap_receive(request_message);

    TEST_ASSERT_EQUAL(COAP_NO_ERROR, result);

    oc_message_unref(request_message);
}

void test_channel_common_input_coap_message__unregistered_resource_fails(void)
{
    // register resource
    const struct nx_channel_resource_props pc_props = {
        .uri = "/c",
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

    // WARNING: if we instantiate a *pointer* to a `coap_packet_t` here, then we
    // will get segfaults because the address of the packet pointer will be in a
    // different address space (CMock?) than the pointers of internal packet
    // struct members, for example, `uint8_t* buffer`; so instantiate the entire
    // object here to allow `memset` operations later.

    coap_packet_t request_packet;
    memset((void*) &response_packet, 0x00, sizeof(coap_packet_t));
    // initialize packet: confirmable message (GET) with arbitrary message ID
    coap_udp_init_message(&request_packet, COAP_TYPE_CON, 1, 456);
    // set the request URI path
    coap_set_header_uri_path(&request_packet, "/nx/nil", strlen("/nx/nil"));

    oc_message_t* request_message = oc_allocate_message();
    if (request_message)
    {
        // set the message length based on the result of data serialization
        request_message->length =
            coap_serialize_message(&request_packet, request_message->data);
    }
    // determines if the request can be handled or not based on resource model
    bool handled = oc_ri_invoke_coap_entity_handler(
        &request_packet, &response_packet, (void*) RESP_BUFFER, &FAKE_ENDPOINT);
    TEST_ASSERT_FALSE(handled);
    TEST_ASSERT_EQUAL(NOT_FOUND_4_04, response_packet.code);

    oc_message_unref(request_message);
}

void test_channel_common_input_coap_message__unregistered_resource_handler_fails(
    void)
{
    // register resource
    const struct nx_channel_resource_props pc_props = {
        .uri = "/c",
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

    // WARNING: if we instantiate a *pointer* to a `coap_packet_t` here, then we
    // will get segfaults because the address of the packet pointer will be in a
    // different address space (CMock?) than the pointers of internal packet
    // struct members, for example, `uint8_t* buffer`; so instantiate the entire
    // object here to allow `memset` operations later.

    coap_packet_t request_packet;
    // initialize packet: confirmable message (DELETE) with arbitrary message ID
    // DELETE handler has not been registered!
    coap_udp_init_message(&request_packet, COAP_TYPE_CON, 4, 789);
    // set the request URI path
    coap_set_header_uri_path(&request_packet, "/c", strlen("/c"));

    oc_message_t* request_message = oc_allocate_message();
    if (request_message)
    {
        // set the message length based on the result of data serialization
        request_message->length =
            coap_serialize_message(&request_packet, request_message->data);
    }

    // determines if the request can be handled or not based on resource model
    bool handled = oc_ri_invoke_coap_entity_handler(
        &request_packet, &response_packet, (void*) RESP_BUFFER, &FAKE_ENDPOINT);
    TEST_ASSERT_FALSE(handled);
    TEST_ASSERT_EQUAL(METHOD_NOT_ALLOWED_4_05, response_packet.code);

    oc_message_unref(request_message);
}

void test_channel_common_network_layer__receive_event_ok(void)
{
    // register resource
    const struct nx_channel_resource_props pc_props = {
        .uri = "/c",
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

    // WARNING: if we instantiate a *pointer* to a `coap_packet_t` here, then we
    // will get segfaults because the address of the packet pointer will be in a
    // different address space (CMock?) than the pointers of internal packet
    // struct members, for example, `uint8_t* buffer`; so instantiate the entire
    // object here to allow `memset` operations later.

    coap_packet_t request_packet;
    // initialize packet: confirmable message (GET) with arbitrary message ID
    coap_udp_init_message(&request_packet, COAP_TYPE_CON, 1, 123);
    // set the request URI path
    coap_set_header_uri_path(&request_packet, "/c", strlen("/c"));

    oc_message_t* request_message = oc_allocate_message();
    if (request_message)
    {
        // set the message length based on the result of data serialization
        request_message->length =
            coap_serialize_message(&request_packet, request_message->data);
    }

    // `oc_main_poll` will keep on running `oc_process_run` until there are
    // no more events and no more poll requests from OC processes.
    // First `oc_process_run` first honors the poll request on the OC Network
    // Events process that was introduced to it by the call to
    // `oc_network_event`. Running the process poll results in a new event
    // delivered to the OC Messaging Buffer process running this event
    // results in a new event delivered to the CoAP engine process. Second
    // `oc_process_run` processes CoAP message (no process polls pending)
    // and delivers it to the registered resource handler.
    TEST_ASSERT_EQUAL(0, oc_process_nevents());

    oc_network_event(request_message);

    // `nxp_common_request_processing` would result in a call to
    // `nexus_channel_core_process`
    nexus_channel_res_payg_credit_get_handler_ExpectAnyArgs();
    nexus_channel_core_process(0);
    TEST_ASSERT_EQUAL(0, oc_process_nevents());
    oc_message_unref(request_message);
}

void test_channel_common__no_iotivity_processes_to_run__returns_idle_time(void)
{
    uint32_t next_call = nexus_channel_core_process(0);
    TEST_ASSERT_EQUAL(next_call,
                      NEXUS_COMMON_IDLE_TIME_BETWEEN_PROCESS_CALL_SECONDS);
    TEST_ASSERT_EQUAL(0, oc_process_nevents());
}

void test_channel_common_apply_origin_command__unsupported_command__returns_false(
    void)
{
    struct nexus_channel_om_command_message message = {0};
    message.type = NEXUS_CHANNEL_OM_COMMAND_TYPE_INVALID;

    TEST_ASSERT_FALSE(nexus_channel_core_apply_origin_command(&message));

    // Move the functions to the 'supported/returns true' test once suppported.

    message.type = NEXUS_CHANNEL_OM_COMMAND_TYPE_ACCESSORY_ACTION_UNLOCK;
    TEST_ASSERT_FALSE(nexus_channel_core_apply_origin_command(&message));

    message.type = NEXUS_CHANNEL_OM_COMMAND_TYPE_ACCESSORY_ACTION_UNLINK;
    TEST_ASSERT_FALSE(nexus_channel_core_apply_origin_command(&message));
}

void test_channel_common_apply_origin_command__supported_command__returns_true(
    void)
{
    struct nexus_channel_om_command_message message = {0};

    message.type = NEXUS_CHANNEL_OM_COMMAND_TYPE_CREATE_ACCESSORY_LINK_MODE_3;
    nxp_channel_notify_event_Expect(NXP_CHANNEL_EVENT_LINK_HANDSHAKE_STARTED);
    nxp_common_request_processing_Expect();
    TEST_ASSERT_TRUE(nexus_channel_core_apply_origin_command(&message));

    message.type = NEXUS_CHANNEL_OM_COMMAND_TYPE_GENERIC_CONTROLLER_ACTION;
    // clear links is the only generic controller command implemented,
    // which will request main processing time
    nxp_common_request_processing_Expect();
    TEST_ASSERT_TRUE(nexus_channel_core_apply_origin_command(&message));
}
