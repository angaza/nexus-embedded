/**
 * @file clock.h
 * @author Angaza
 * @date 25 February 2020
 * @brief File containing monotonic clock management.
 *
 * This file is an example of the product-side code required to
 * track (and report) elapsed time to the Nexus Keycode library. The library
 * assumes that the system/product has some way of reliably keeping track of
 * monotonically elapsing real-world time.
 *
 * Specifically, the Nexus Keycode library requires the product code to report a
 * periodic notification (typically every 60 seconds) as time elapses. Calendar
 * datetime is not used.
 */

#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>
#include <time.h>

/* @brief Initialize product-side monotonic clock interface.
 *
 * Simply provides an initial 'reference reading' of the system CLOCK_MONOTONIC.
 *
 * Should be called exactly once when the program is started, and only called
 * again on program restarts (e.g. after power cycle or reboot).
 *
*/
void clock_init(void);

/* @brief Called periodically by the program code to consume credit.
 *
 * The frequency at which this function is called will determine the
 * resolution/granularity of the timekeeping.
 *
 * It is recommended to call this function at least once per minute, to
 * ensure that credit expires in a predictable way. If this function
 * is never called, credit will never expire.
*/
void clock_consume_credit(void);

/* @brief Convenience function to return seconds since the epoch.
 *
 * This is used both internally within the `clock` module, and externally
 * by code that wishes to use the monotonic clock for relative, low frequency
 * timekeeping measurements (on the order of seconds).
 *
 * @return current MONOTONIC_TIME in seconds
 *
 */
uint32_t clock_read_monotonic_time_seconds(void);

/* @brief Convenience function returning difference between two time readings.
 *
 * This function assumes that `previous_time_secs` is a value obtained from
 * a previous call to `clock_read_monotonic_time`. Internally, this function
 * calls `clock_read_monotonic_time`, and computes the difference in seconds.
 *
 * The return value is guaranteed to be greater than or equal to 0. The type
 * of uint32_t is useful as the Nexus Keycode library interfaces require
 * unsigned 32-bit values for any time measurements.
 *
 * @param previous_time_secs A previous reading of the CLOCK_MONOTONIC
 * @return seconds elapsed since CLOCK_MONOTONIC reported `previous_time_secs`
 *
 */
uint32_t clock_seconds_elapsed_since(const time_t previous_time_secs);

#endif
