#include "src/internal_keycode_config.h"
#include "include/nxp_common.h"
#include "include/nxp_keycode.h"
#include "include/nxp_channel.h"
#include "src/nexus_channel_res_payg_credit.h"
#include "oc/include/oc_network_events.h"

static bool quit = false;

// fake product-side resources
static void stub_resource_get_handler(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
    (void) request;
    (void) interfaces;
    (void) user_data;
}
static void stub_resource_post_handler(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data)
{
    (void) request;
    (void) interfaces;
    (void) user_data;
}

// COMMON

/** Stub application to run static analysis over compiled artifact, and
 * estimate code size.
 */
int main(void)
{
    nx_common_init(0);
    // arbitrary 5 seconds
    (void) nx_common_process(5);

#if NEXUS_KEYCODE_ENABLED
    // simulate receiving a keycode
    struct nx_keycode_complete_code dummy_keycode = {
        .keys = "*123456789#",
        .length = 11
    };
    (void) nx_keycode_handle_complete_keycode(&dummy_keycode);

    enum nx_keycode_custom_flag flag = NX_KEYCODE_CUSTOM_FLAG_RESTRICTED;
    (void) nx_keycode_set_custom_flag(flag);
#endif

#if NEXUS_CHANNEL_CORE_ENABLED
    // XXX: create nx_receive_message or similar interface to pass in raw bytes from
    // a transport layer to Nexus (security layer). Abstract away all OC.

    #if NEXUS_CHANNEL_LINK_SECURITY_ENABLED
    // simulate receiving an origin command
    (void) nx_channel_handle_origin_command(
        NX_CHANNEL_ORIGIN_COMMAND_BEARER_TYPE_ASCII_DIGITS,
        "123456789",
        10
    );

    (void) nx_channel_link_count();
    #endif

    const oc_interface_mask_t if_mask_arr[] = {OC_IF_BASELINE, OC_IF_RW};
    const struct nx_channel_resource_props pc_props = {
        .uri = "/c",
        .resource_type = "x.stub.resource",
        .rtr = 65535,
        .num_interfaces = 2,
        .if_masks = if_mask_arr,
        .get_handler = stub_resource_get_handler,
        .get_secured = false,
        .post_handler = NULL,
        .post_secured = false}; // unsecured
    (void) nx_channel_register_resource(&pc_props);
    (void) nx_channel_register_resource_handler(
        "/c", OC_POST, stub_resource_post_handler, false); // unsecured

    struct nx_id fake_id = {0, 12345678};
    uint8_t dummy_data[10];
    memset(&dummy_data, 0xAB, sizeof(dummy_data));
    (void) nx_channel_network_receive(dummy_data, 10, &fake_id);

#endif // NEXUS_CHANNEL_CORE_ENABLED
    while (!quit)
    {
#if NEXUS_CHANNEL_CORE_ENABLED
        (void) oc_main_poll();
#endif // NEXUS_CHANNEL_CORE_ENABLED
    }

    nx_common_shutdown();
    return 0;
}

bool nxp_common_nv_write(const struct nx_common_nv_block_meta block_meta, void* write_buffer)
{
    (void) block_meta;
    (void) write_buffer;
    return true;
}

bool nxp_common_nv_read(const struct nx_common_nv_block_meta block_meta, void* read_buffer)
{
    (void) block_meta;
    (void) read_buffer;
    return true;
}

void nxp_common_request_processing(void)
{
    return;
}

// KEYCODE

#if NEXUS_KEYCODE_ENABLED
bool nxp_keycode_feedback_start(enum nxp_keycode_feedback_type feedback_type)
{
    (void) feedback_type;
    return true;
}

bool nxp_keycode_payg_credit_add(uint32_t credit)
{
    return credit != 0;
}

bool nxp_keycode_payg_credit_set(uint32_t credit)
{
    (void) credit;
    return true;
}

bool nxp_keycode_payg_credit_unlock(void)
{
    return true;
}

struct nx_common_check_key nxp_keycode_get_secret_key(void)
{
    const struct nx_common_check_key stub_key = {0};
    return stub_key;
}

uint32_t nxp_keycode_get_user_facing_id(void)
{
    return 123456789;
}

enum nxp_keycode_passthrough_error nxp_keycode_passthrough_keycode(
        const struct nx_keycode_complete_code* passthrough_keycode)
{
    (void) passthrough_keycode;
    return NXP_KEYCODE_PASSTHROUGH_ERROR_NONE;
}

void nxp_keycode_notify_custom_flag_changed(enum nx_keycode_custom_flag flag, bool value)
{
    (void) flag;
    (void) value;
}

#endif // NEXUS_KEYCODE_ENABLED

// KEYCODE + CHANNEL

#if (NEXUS_KEYCODE_ENABLED || defined(CONFIG_NEXUS_CHANNEL_USE_PAYG_CREDIT_RESOURCE))

enum nxp_common_payg_state nxp_common_payg_state_get_current(void)
{
    return NXP_COMMON_PAYG_STATE_ENABLED;
}

uint32_t nxp_common_payg_credit_get_remaining(void)
{
    return 12345678;
}

#endif // (NEXUS_KEYCODE_ENABLED || defined(CONFIG_NEXUS_CHANNEL_USE_PAYG_CREDIT_RESOURCE))

// CHANNEL CORE

#if NEXUS_CHANNEL_CORE_ENABLED

uint32_t nxp_channel_random_value(void)
{
    return 123456;
}

void nxp_channel_notify_event(enum nxp_channel_event_type event)
{
    (void) event;
}

struct nx_id nxp_channel_get_nexus_id(void)
{
    struct nx_id id = {0, 12345678};
    return id;
}

nx_channel_error
nxp_channel_network_send(const void* const bytes_to_send,
                         uint32_t bytes_count,
                         const struct nx_id* const source,
                         const struct nx_id* const dest,
                         bool is_multicast)
{
    (void) bytes_to_send;
    (void) bytes_count;
    (void) source;
    (void) dest;
    (void) is_multicast;
    return NX_CHANNEL_ERROR_NONE;
}

// NEXUS CHANNEL-ONLY

#if NEXUS_CHANNEL_LINK_SECURITY_ENABLED

struct nx_common_check_key nxp_channel_symmetric_origin_key(void)
{
    const struct nx_common_check_key stub_key = {0};
    return stub_key;
}


nx_channel_error nxp_channel_payg_credit_set(uint32_t remaining)
{
    (void) remaining;
    return NX_CHANNEL_ERROR_NONE;
}

nx_channel_error nxp_channel_payg_credit_unlock(void)
{
    return NX_CHANNEL_ERROR_NONE;
}

#endif
#endif
