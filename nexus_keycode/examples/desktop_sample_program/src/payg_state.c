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
#include "nexus_keycode_port.h"
#include "nonvol.h"
#include "processing.h"

/**
 * NOTE: This implementation is very basic and not suitable for most
 * PAYG applications. We recommend implementing more robust PAYG state
 * functionality.
 */

static struct
{
    NEXUS_PACKED_STRUCT payg_state_struct stored;
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

enum payg_state port_payg_state_get_current(void)
{
    if (_this.stored.is_unlocked)
    {
        return PAYG_STATE_UNLOCKED;
    }
    if (_this.stored.credit > 0)
    {
        return PAYG_STATE_ENABLED;
    }
    return PAYG_STATE_DISABLED;
}

bool update_payg_state(bool is_unlocked, uint32_t credit)
{
    _this.stored.is_unlocked = is_unlocked;
    _this.stored.credit = credit;
    // Notify the product code on state changes.
    enum payg_state current_payg_state = port_payg_state_get_current();

    if (_this.last_payg_state != current_payg_state)
    {
        // our 'port_request_processing' is called by Nexus Keycode to
        // request processing, but we also use it internally to update
        // and read the current time
        port_request_processing();
        _this.last_payg_state = current_payg_state;
    }

    // write updated state to NV
    payg_state_update_nv();
    return true;
}

void payg_state_update_nv(void)
{
    prod_nv_write_payg_state(sizeof(struct payg_state_struct),
                             (uint8_t*) &_this.stored);
}

bool port_payg_credit_add(uint32_t credit)
{
    return update_payg_state(_this.stored.is_unlocked,
                             _this.stored.credit + credit);
}

bool port_payg_credit_set(uint32_t credit)
{
    return update_payg_state(false, credit);
}

bool port_payg_credit_unlock(void)
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
