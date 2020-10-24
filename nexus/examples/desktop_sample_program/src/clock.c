/** \file clock.c
 * \brief A mock implementation of PAYG timekeeping.
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include <math.h>

#include "clock.h"
#include "nxp_channel.h"
#include "payg_state.h"

/**
 * NOTE: This implementation assumes a POSIX compliant system, where
 * `clock_gettime(CLOCK_MONOTONIC, ..)` returns seconds since the epoch.
 */

// Used for periodic credit consumption (see `clock_consume_credit`)
static struct
{
    time_t prev_clock_secs;
} _this;

void clock_init(void)
{
    // In a production system, it is recommended to instead determine how
    // much time has elapsed while the program was closed, and account
    // for this time in the initialization (so that the Nexus Keycode library is
    // informed of the passed time, and the appropriate delta in time/credit
    // is consumed).
    _this.prev_clock_secs = clock_read_monotonic_time_seconds();
}

void clock_consume_credit(void)
{
    // Reuse convenience function to calculate seconds elapsed.
    const uint32_t secs_elapsed =
        clock_seconds_elapsed_since(_this.prev_clock_secs);
    payg_state_consume_credit(secs_elapsed);

    // Update static 'latest RTC reading' variable. Time spent executing inside
    // the `clock_consume_credit` function should not 'count against' the user.
    _this.prev_clock_secs = clock_read_monotonic_time_seconds();
}

uint32_t clock_read_monotonic_time_seconds(void)
{
    struct timespec ts;
    // Production code should consider errors returned from `clock_gettime`.
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t) ts.tv_sec;
}

uint32_t clock_seconds_elapsed_since(const time_t previous_time_secs)
{
    const time_t current_secs = clock_read_monotonic_time_seconds();
    const double secs_elapsed = difftime(current_secs, previous_time_secs);
    if (secs_elapsed > 0)
    {
        return (uint32_t) floor(secs_elapsed);
    }
    return 0;
}
