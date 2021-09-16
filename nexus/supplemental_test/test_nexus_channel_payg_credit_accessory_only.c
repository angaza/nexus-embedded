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
    // nxp_channel_payg_credit_set_ExpectAndReturn(0, NX_CHANNEL_ERROR_NONE);
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

void test_payg_credit_init__is_an_accessory__initializes_no_credit(void)
{
    // we perform a custom setup for this function, as we want to simulate
    // a link being present before initializing the PAYG credit module
    nexus_channel_core_shutdown();
    oc_nexus_testing_reinit_mmem_lists();
    oc_message_unref(G_OC_MESSAGE);

    nexus_channel_core_init();
    nexus_channel_res_link_hs_init();
    nexus_channel_link_manager_init();

    // re-initialize payg credit, should detect that it is independent and
    // unlinked and request to set PAYG credit to 0
    nxp_common_payg_state_get_current_ExpectAndReturn(
        NXP_COMMON_PAYG_STATE_ENABLED);
    nxp_common_payg_credit_get_remaining_ExpectAndReturn(54021);
    nxp_channel_payg_credit_set_ExpectAndReturn(0, NX_CHANNEL_ERROR_NONE);
    nexus_channel_res_payg_credit_init();

    enum nexus_channel_payg_credit_operating_mode mode =
        _nexus_channel_res_payg_credit_get_credit_operating_mode();
    TEST_ASSERT_EQUAL(NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_INDEPENDENT,
                      mode);
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
    TEST_ASSERT_EQUAL(NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_INDEPENDENT,
                      mode);
}
