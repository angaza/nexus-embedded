#include "src/internal_keycode_config.h"
#include "include/nxp_core.h"
#include "include/nxp_keycode.h"
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
