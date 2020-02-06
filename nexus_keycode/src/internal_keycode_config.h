/** \file
 * Nexus Protocol Internal Configuration Parameters
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#ifndef __NEXUS__KEYCODE__SRC__INTERNAL_KEYCODE_CONFIG_H_
#define __NEXUS__KEYCODE__SRC__INTERNAL_KEYCODE_CONFIG_H_

#include "include/nx_keycode.h"

// In most cases, there is no need to modify the values of this file.

// Identifies the Nexus keycode protocol public 'release version'.
#define NEXUS_PROTOCOL_RELEASE_VERSION_COUNT 1

#define NEXUS_KEYCODE_PROTOCOL_NO_STOP_LENGTH UINT8_MAX

// Update the NEXUS_KEYCODE_PROTOCOL_STOP_LENGTH based on protocol
#if NEXUS_KEYCODE_PROTOCOL == NEXUS_KEYCODE_PROTOCOL_FULL
// Number of digits in a 'full' activation message
#define NEXUS_KEYCODE_PROTOCOL_STOP_LENGTH NEXUS_KEYCODE_PROTOCOL_NO_STOP_LENGTH
#define NEXUS_KEYCODE_PROTOCOL_FULL_ACTIVATION_MESSAGE_LENGTH 14
#else
#define NEXUS_KEYCODE_PROTOCOL_STOP_LENGTH 14
#endif

// Compile-time parameter checks
#ifndef NEXUS_KEYCODE_PROTOCOL
#error "NEXUS_KEYCODE_PROTOCOL must be defined."
#endif

#if NEXUS_KEYCODE_PROTOCOL != NEXUS_KEYCODE_PROTOCOL_FULL
#if NEXUS_KEYCODE_PROTOCOL != NEXUS_KEYCODE_PROTOCOL_SMALL
#error "NEXUS_KEYCODE_PROTOCOL must be SMALL or FULL version."
#endif
#endif

#if NEXUS_KEYCODE_PROTOCOL == NEXUS_KEYCODE_PROTOCOL_FULL
#ifndef NEXUS_KEYCODE_PRO_FACTORY_QC_SHORT_LIFETIME_MAX
#error "NEXUS_KEYCODE_PRO_FACTORY_QC_SHORT_LIFETIME_MAX is undefined."
#endif
#ifndef NEXUS_KEYCODE_PRO_FACTORY_QC_LONG_LIFETIME_MAX
#error "NEXUS_KEYCODE_PRO_FACTORY_QC_LONG_LIFETIME_MAX is undefined."
#endif

#if NEXUS_KEYCODE_PRO_FACTORY_QC_SHORT_LIFETIME_MAX > 15
#error "NEXUS_KEYCODE_PRO_FACTORY_QC_SHORT_LIFETIME_MAX value is > 15."
#endif
#if NEXUS_KEYCODE_PRO_FACTORY_QC_LONG_LIFETIME_MAX > 15
#error "NEXUS_KEYCODE_PRO_FACTORY_QC_LONG_LIFETIME_MAX value is > 15."
#endif
#endif

#if NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX != 0
#ifndef NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT
#error "NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT is undefined."
#endif
#ifndef NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT
#error                                                                         \
    "NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT is undefined."
#endif
#if NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT == 0
#error                                                                         \
    "NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT must be nonzero."
#endif
#if NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX > 255
#error "NEXUS_KEYCODE_PRO_FACTORY_QC_LONG_LIFETIME_MAX value is > 255."
#endif
#if NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT > 255
#error "NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT value is > 255."
#endif
#if NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT > 3600
#error "NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT value is > 3600."
#endif
#endif

// Macro to expose certain functions during unit tests
#ifdef NEXUS_INTERNAL_IMPL_NON_STATIC
#define NEXUS_IMPL_STATIC
#else
#define NEXUS_IMPL_STATIC static
#endif

// use static asserts by default only under c11 and above
// Do not use static asserts for the frama-c build
#ifndef NEXUS_STATIC_ASSERT
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#ifndef __FRAMAC__
#define NEXUS_STATIC_ASSERT(b, m) _Static_assert(b, m)
#else
#define NEXUS_STATIC_ASSERT(b, m)
#endif
#else
#define NEXUS_STATIC_ASSERT(b, m)
#endif
#endif

// do not use runtime asserts by default
#ifndef NEXUS_ASSERT
#if defined(DEBUG) && !defined(NDEBUG)
#include <assert.h>
#define NEXUS_ASSERT(b, m) assert(b)
#define NEXUS_ASSERT_FAIL_IN_DEBUG_ONLY(b, m) assert(b)
#elif defined(NEXUS_USE_DEFAULT_ASSERT)
#include <assert.h>
#define NEXUS_ASSERT(b, m) assert(b)
#define NEXUS_ASSERT_FAIL_IN_DEBUG_ONLY(b, m)
#else
#define NEXUS_ASSERT(b, m)
#define NEXUS_ASSERT_FAIL_IN_DEBUG_ONLY(b, m)
#endif
#endif

#endif
