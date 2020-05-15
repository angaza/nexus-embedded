#include "src/internal_keycode_config.h"
#include "include/nxp_core.h"
#include "include/nxp_keycode.h"
#include "include/nxp_channel.h"
#include "oc/include/oc_network_events.h"

static bool quit = false;

/** Stub application to run static analysis over compiled artifact, and
 * estimate code size.
 */
int main(void)
{
    nx_core_init();
    // arbitrary 5 seconds
    nx_core_process(5);

    // simulate receiving an origin command
    nx_channel_handle_origin_command(
        NX_CHANNEL_ORIGIN_COMMAND_BEARER_TYPE_ASCII_DIGITS,
        "123456789",
        10
    );

    // simulate receiving a keycode
    struct nx_keycode_complete_code dummy_keycode = {
        .keys = "*123456789#",
        .length = 11
    };
    nx_keycode_handle_complete_keycode(&dummy_keycode);

    // XXX: create nx_receive_message or similar interface to pass in raw bytes from
    // a transport layer to Nexus (security layer). Abstract away all OC.
    while (!quit)
    {
        oc_main_poll();
    }

    nx_core_shutdown();
    return 0;
}

bool port_nv_init(void)
{
    return true;
}

bool nxp_core_nv_write(const struct nx_core_nv_block_meta block_meta, void* write_buffer)
{
    (void) block_meta;
    (void) write_buffer;
    return true;
}

bool nxp_core_nv_read(const struct nx_core_nv_block_meta block_meta, void* read_buffer)
{
    (void) block_meta;
    (void) read_buffer;
    return true;
}

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

enum nxp_core_payg_state nxp_core_payg_state_get_current(void)
{
    return NXP_CORE_PAYG_STATE_ENABLED;
}

struct nx_core_check_key nxp_keycode_get_secret_key(void)
{
    const struct nx_core_check_key stub_key = {0};
    return stub_key;
}

uint32_t nxp_keycode_get_user_facing_id(void)
{
    return 123456789;
}

uint32_t nxp_core_uptime_seconds(void)
{
    return 100;
}

void nxp_core_request_processing(void)
{
}

enum nxp_keycode_passthrough_error nxp_keycode_passthrough_keycode(
        const struct nx_keycode_complete_code* passthrough_keycode)
{
    (void) passthrough_keycode;
    return NXP_KEYCODE_PASSTHROUGH_ERROR_NONE;
}

void nxp_core_random_init(void)
{
    return;
}

uint32_t nxp_core_random_value(void)
{
    return 123456;
}

struct nx_core_check_key nxp_channel_symmetric_origin_key(void)
{
    const struct nx_core_check_key stub_key = {0};
    return stub_key;
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
                         const struct nx_ipv6_address* const source_address,
                         const struct nx_ipv6_address* const dest_address,
                         bool is_multicast)
{
    (void) bytes_to_send;
    (void) bytes_count;
    (void) source_address;
    (void) dest_address;
    (void) is_multicast;
    return NX_CHANNEL_ERROR_NONE;
}

void oc_clock_init(void)
{
    return;
}

oc_clock_time_t oc_clock_time(void)
{
    return (oc_clock_time_t) 0;
}
