/** \file processing.c
 * \brief Implementation of keycode processing functions.
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "processing.h"
#include "clock.h"
#include "nxp_core.h"
#include "nxp_keycode.h"
#include "payg_state.h"
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>

static const uint32_t ONE_HOUR_IN_SECONDS = 3600;

static struct
{
    bool nx_processing_requested;

    // Implementation-specific timer to call `nx_keycode_process` periodically.
    timer_t timerid;
    struct sigevent sev;

    struct itimerspec its;

    uint32_t last_uptime_seconds;

    // We track this so we can periodically store PAYG state to NV,
    // since it must be stored not only when keycodes are entered, but also
    // at least every hour.
    uint32_t seconds_since_payg_state_backup;
} _this;

/**
 * Thread start routine to notify the application when the timer expires.
 * Each time the timer expires, this routine is executed.
 */
void timer_thread(/*union sigval val*/)
{
    // Reuse existing `port_request_processing` to add a new
    // `nx_keycode_process` deferred call.
    nxp_core_request_processing();
}

void processing_init(void)
{
    _this.timerid = NULL;
    _this.nx_processing_requested = false;

    // Setting this to '0' will ensure any logic which needs to 'periodically'
    // write to NV will do so soon after boot as well.
    _this.last_uptime_seconds = 0;
    _this.seconds_since_payg_state_backup = 0;

    // Configure the signal fired when the timer expired
    _this.sev.sigev_notify = SIGEV_THREAD;
    _this.sev.sigev_value.sival_ptr = &_this.timerid;
#ifdef __cplusplus
    _this.sev.sigev_notify_function = (void (*)(sigval)) timer_thread;
#else
    _this.sev.sigev_notify_function = timer_thread;
#endif
    _this.sev.sigev_notify_attributes = NULL;

    // Create a timer for use by this module
    timer_create(CLOCK_MONOTONIC, &_this.sev, &_this.timerid);
}

void processing_deinit(void)
{
    if (_this.timerid != NULL)
    {
        timer_delete(_this.timerid);
    }
}

// This function executes periodically from the main loop.
void processing_execute(void)
{
    const uint32_t cur_uptime = clock_read_monotonic_time_seconds();
    if (_this.nx_processing_requested)
    {
        _this.nx_processing_requested = false;
        uint32_t max_secs_to_next_call = nx_core_process(cur_uptime);

        _this.its.it_value.tv_sec = max_secs_to_next_call;
        _this.its.it_value.tv_nsec = 0;
        _this.its.it_interval.tv_sec = 0;
        _this.its.it_interval.tv_nsec = 0;
        timer_settime(_this.timerid, 0, &_this.its, 0);
    }

    const uint32_t delta = cur_uptime - _this.last_uptime_seconds;
    _this.last_uptime_seconds = cur_uptime;

    _this.seconds_since_payg_state_backup += delta;

    if (_this.seconds_since_payg_state_backup > ONE_HOUR_IN_SECONDS)
    {
        payg_state_update_nv();
        _this.seconds_since_payg_state_backup = 0;
    }
}

/**
 * Must not call `nx_keycode_process` within `port_request_processing`, as the
 * Nexus Keycode library assumes that the currently executing code will complete
 * before `nx_keycode_process` is called. `port_request_processing` is simply a
 * request to call `nx_keycode_process` during the next program 'idle' time.
 */
void nxp_core_request_processing(void)
{
    _this.nx_processing_requested = true;
}

// Enter a loop where we simply execute
void processing_idle_loop(uint32_t seconds)
{
    const uint32_t stop_time = clock_read_monotonic_time_seconds() + seconds;

    while (clock_read_monotonic_time_seconds() < stop_time)
    {
        processing_execute();
    }
}
