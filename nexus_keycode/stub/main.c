#include "src/internal_keycode_config.h"
#include "include/nexus_keycode_port.h"
#include "src/nexus_keycode_core.h"
#include "src/nexus_keycode_mas.h"
#include "src/nexus_keycode_util.h"
#include <stdio.h>

/** Stub application to run static analysis over compiled artifact.
 */
int main(void)
{
    printf("Stub Present");
}

bool port_nv_init(void)
{
    return true;
}

bool port_nv_write(const struct nx_nv_block_meta block_meta, void* write_buffer)
{
    (void) block_meta;
    (void) write_buffer;
    return true;
}

bool port_nv_read(const struct nx_nv_block_meta block_meta, void* read_buffer)
{
    (void) block_meta;
    (void) read_buffer;
    return true;
}

bool port_feedback_start(enum port_feedback_type feedback_type)
{
    (void) feedback_type;
    return true;
}

bool port_payg_credit_add(uint32_t credit)
{
    return credit != 0;
}

bool port_payg_credit_set(uint32_t credit)
{
    (void) credit;
    return true;
}

bool port_payg_credit_unlock(void)
{
    return true;
}

enum payg_state port_payg_state_get_current(void)
{
    return PAYG_STATE_ENABLED;
}

struct nx_check_key port_identity_get_secret_key(void)
{
    const struct nx_check_key stub_key = {0};
    return stub_key;
}

uint32_t port_identity_get_serial_id(void)
{
    return 123456789;
}

uint32_t port_uptime_seconds(void)
{
    return 100;
}

void port_request_processing(void)
{
}

enum port_passthrough_error port_passthrough_keycode(
        const struct nx_keycode_complete_code* passthrough_keycode)
{
    (void) passthrough_keycode;
    return PORT_PASSTHROUGH_ERROR_NONE;
}
