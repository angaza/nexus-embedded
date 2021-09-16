/** \file product_payg_state_manager.c
 * \brief Example implementation of product-side PAYG state management
 * \author Angaza
 * \copyright 2021 Angaza, Inc
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 *
 * Example of how to persist and manage PAYG state/credit. See also
 * 'nxp_implementations.c' for places where this information is consumed
 * by the Nexus library.
 */

#include <assert.h>
// for memcmp
#include <string.h>
// for k_timer
#include <zephyr.h>

#include "flash_filesystem.h"
#include "product_nexus_identity.h"
#include "product_payg_state_manager.h"

#define PRODUCT_PAYG_STATE_MANAGER_ONE_MINUTE_IN_SECONDS (60U)
#define PRODUCT_PAYG_STATE_MANAGER_ONE_HOUR_IN_SECONDS (3600U)

static struct
{
    uint32_t credit_remaining;
    uint16_t seconds_elapsed_since_hourly_nv_write;
    // used to periodically reduce credit remaining when system is running
    struct k_timer credit_update_timer;
} _this;

static void _product_payg_state_manager_write_credit_to_nv(void)
{
    // update credit value in NV
    const int bytes_written = flash_filesystem_write_product_nv(
        FLASH_FILESYSTEM_PRODUCT_NV_ID_PAYG_MANAGER_CREDIT_REMAINING,
        &_this.credit_remaining,
        sizeof(_this.credit_remaining));
    assert(bytes_written == sizeof(_this.credit_remaining));
}

// Does not run in the timer interrupt, because NV writes can take significant
// time to perform.
void _product_payg_manager_update_credit_and_nv_on_workqueue(
    struct k_work* work)
{
    (void) work;

    if ((_this.credit_remaining ==
         PRODUCT_PAYG_STATE_MANAGER_UNLOCKED_CREDIT_SENTINEL) ||
        (_this.credit_remaining == 0))
    {
        // Would have already written to NV on unlock or transition to
        // 0/disabled, do nothing.
        return;
    }

    assert(_this.credit_remaining > 0);

    if (_this.credit_remaining <
        PRODUCT_PAYG_STATE_MANAGER_ONE_MINUTE_IN_SECONDS)
    {
        _this.credit_remaining = 0;
        // trigger write to NV immediately on transition to 'disabled' state
        _this.seconds_elapsed_since_hourly_nv_write =
            PRODUCT_PAYG_STATE_MANAGER_ONE_HOUR_IN_SECONDS + 1;
    }
    else
    {
        assert(_this.credit_remaining >
               PRODUCT_PAYG_STATE_MANAGER_ONE_MINUTE_IN_SECONDS);
        _this.credit_remaining -=
            PRODUCT_PAYG_STATE_MANAGER_ONE_MINUTE_IN_SECONDS;
        _this.seconds_elapsed_since_hourly_nv_write +=
            PRODUCT_PAYG_STATE_MANAGER_ONE_MINUTE_IN_SECONDS;
    }

    // every elapsed hour, persist remaining credit to NV storage
    if (_this.seconds_elapsed_since_hourly_nv_write >
        PRODUCT_PAYG_STATE_MANAGER_ONE_HOUR_IN_SECONDS)
    {
        _product_payg_state_manager_write_credit_to_nv();
        _this.seconds_elapsed_since_hourly_nv_write = 0;
    }
}

// Defines a 'workqueue' item that will run
// `_product_payg_manager_update_credit_and_nv_on_workqueue` after
// `k_work_submit(&update_credit_and_nv);` is called
K_WORK_DEFINE(update_credit_and_nv,
              _product_payg_manager_update_credit_and_nv_on_workqueue);

// Used to periodically (every 60 seconds) schedule a task to update PAYG
// credit. The update is not done directly within this function because
// updating NV can take significant time, and we do not want to block in
// the timer interrupt here.
void _product_payg_manager_timer_handler(struct k_timer* timer_id)
{
    (void) timer_id;
    k_work_submit(&update_credit_and_nv);
}

void product_payg_state_manager_init(void)
{
    _this.seconds_elapsed_since_hourly_nv_write = 0;

    const int credit_bytes_read = flash_filesystem_read_product_nv(
        FLASH_FILESYSTEM_PRODUCT_NV_ID_PAYG_MANAGER_CREDIT_REMAINING,
        &_this.credit_remaining,
        sizeof(_this.credit_remaining));

    // If there was a failure to read credit from NV, set remaining credit to 0
    if (credit_bytes_read != sizeof(_this.credit_remaining))
    {
        _this.credit_remaining = 0;
    }

    // If this device does not have a Nexus/PAYG ID, allow it to be 'unlocked'
    // to allow for factory-line testing.
    const struct nx_id* current_id = product_nexus_identity_get_nexus_id();
    if (memcmp(current_id,
               &PRODUCT_NEXUS_IDENTITY_DEFAULT_NEXUS_ID,
               sizeof(struct nx_id)) == 0)
    {
        _this.credit_remaining =
            PRODUCT_PAYG_STATE_MANAGER_UNLOCKED_CREDIT_SENTINEL;
    }

    _product_payg_state_manager_write_credit_to_nv();
    k_timer_init(
        &_this.credit_update_timer, _product_payg_manager_timer_handler, NULL);
    k_timer_start(&_this.credit_update_timer,
                  K_SECONDS(PRODUCT_PAYG_STATE_MANAGER_ONE_MINUTE_IN_SECONDS),
                  K_SECONDS(PRODUCT_PAYG_STATE_MANAGER_ONE_MINUTE_IN_SECONDS));
}

void product_payg_state_manager_add_credit(uint32_t added_credit)
{
    // Nexus library won't attempt to add credit leading to overflow
    assert((UINT32_MAX - added_credit) > _this.credit_remaining);

    k_timer_stop(&_this.credit_update_timer);
    _this.credit_remaining += added_credit;
    _product_payg_state_manager_write_credit_to_nv();
    k_timer_start(&_this.credit_update_timer,
                  K_SECONDS(PRODUCT_PAYG_STATE_MANAGER_ONE_MINUTE_IN_SECONDS),
                  K_SECONDS(PRODUCT_PAYG_STATE_MANAGER_ONE_MINUTE_IN_SECONDS));
}

void product_payg_state_manager_set_credit(uint32_t set_credit)
{
    k_timer_stop(&_this.credit_update_timer);
    _this.credit_remaining = set_credit;
    _product_payg_state_manager_write_credit_to_nv();
    k_timer_start(&_this.credit_update_timer,
                  K_SECONDS(PRODUCT_PAYG_STATE_MANAGER_ONE_MINUTE_IN_SECONDS),
                  K_SECONDS(PRODUCT_PAYG_STATE_MANAGER_ONE_MINUTE_IN_SECONDS));
}

void product_payg_state_manager_unlock(void)
{
    k_timer_stop(&_this.credit_update_timer);
    _this.credit_remaining =
        PRODUCT_PAYG_STATE_MANAGER_UNLOCKED_CREDIT_SENTINEL;
    _product_payg_state_manager_write_credit_to_nv();
    // No need to restart timer when unit becomes unlocked, PAYG
    // credit will not be decrementing.
}

uint32_t product_payg_state_manager_get_current_credit(void)
{
    return _this.credit_remaining;
}
