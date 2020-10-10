/** \file payg_state.c
 * \brief A mock implementation of one way to track PAYG state.
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "payg_state.h"
#include "identity.h"
#include "nonvol.h"
#include "nxp_channel.h"
#include "nxp_core.h"
#include "nxp_keycode.h"
#include "processing.h"

/**
 * NOTE: This implementation is very basic and not suitable for most
 * PAYG applications. We recommend implementing more robust PAYG state
 * functionality.
 */

static struct
{
    struct payg_state_struct stored;
    bool last_payg_state;
} _this;

void payg_state_init(void)
{
    // attempt to read from NV
    bool valid_payg_state = prod_nv_read_payg_state(
        sizeof(struct payg_state_struct), (uint8_t*) &_this.stored);

    // If we retrieve a valid PAYG state from NV, use it.
    if (valid_payg_state)
    {
        return;
    }

    // Otherwise, initialize to warehouse default (disabled/0 credit) state.
    _this.stored.credit = 0;
    _this.stored.is_unlocked = 0;

    prod_nv_write_payg_state(sizeof(struct payg_state_struct),
                             (uint8_t*) &_this.stored);
}

enum nxp_core_payg_state nxp_core_payg_state_get_current(void)
{
    if (_this.stored.is_unlocked)
    {
        return NXP_CORE_PAYG_STATE_UNLOCKED;
    }
    if (_this.stored.credit > 0)
    {
        return NXP_CORE_PAYG_STATE_ENABLED;
    }
    return NXP_CORE_PAYG_STATE_DISABLED;
}

bool update_payg_state(bool is_unlocked, uint32_t credit)
{
    _this.stored.is_unlocked = is_unlocked;
    _this.stored.credit = credit;
    // Notify the product code on state changes.
    enum nxp_core_payg_state current_payg_state =
        nxp_core_payg_state_get_current();

    if (_this.last_payg_state != current_payg_state)
    {
        // our 'port_request_processing' is called by Nexus Keycode to
        // request processing, but we also use it internally to update
        // and read the current time
        nxp_core_request_processing();
        _this.last_payg_state = current_payg_state;
    }

    // write updated state to NV
    payg_state_update_nv();
    return true;
}

uint32_t nxp_core_payg_credit_get_remaining(void)
{
    return _this.stored.credit;
}

void payg_state_update_nv(void)
{
    prod_nv_write_payg_state(sizeof(struct payg_state_struct),
                             (uint8_t*) &_this.stored);
}

bool nxp_keycode_payg_credit_add(uint32_t credit)
{
    return update_payg_state(_this.stored.is_unlocked,
                             _this.stored.credit + credit);
}

bool nxp_keycode_payg_credit_set(uint32_t credit)
{
    return update_payg_state(false, credit);
}

bool nxp_keycode_payg_credit_unlock(void)
{
    return update_payg_state(true, 0);
}

void payg_state_consume_credit(const uint32_t amount)
{
    uint32_t credit = 0;
    if (amount <= _this.stored.credit)
    {
        credit = _this.stored.credit - amount;
    }
    update_payg_state(_this.stored.is_unlocked, credit);
}

uint32_t payg_state_get_remaining_credit(void)
{
    return _this.stored.credit;
}

// Below functions relate to the PAYG credit resource built-in to Nexus
// Channel.
//
// Used when managing PAYG credit over the Nexus Channel link, or having
// credit managed by another Nexus Channel device.
nx_channel_error nxp_channel_payg_credit_set(uint32_t remaining)
{
    update_payg_state(false, remaining);
    return NX_CHANNEL_ERROR_NONE;
}

nx_channel_error nxp_channel_payg_credit_unlock(void)
{
    update_payg_state(true, 0);
    return NX_CHANNEL_ERROR_NONE;
}
