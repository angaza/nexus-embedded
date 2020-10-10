/** \file internal_common_config.h
 * Internal Nexus common configuration parameters
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#ifndef NEXUS__SRC__INTERNAL_COMMON_CONFIG_H_
#define NEXUS__SRC__INTERNAL_COMMON_CONFIG_H_

#include "include/nx_core.h"
#include "include/shared_oc_config.h"

// Logging macros from 'oc_log' are reused throughout this project.
#include "oc/port/oc_log.h"

// Expose external utils
#include "utils/crc_ccitt.h"
#include "utils/siphash_24.h"

// Macro to expose certain functions during unit tests
#ifdef NEXUS_INTERNAL_IMPL_NON_STATIC
    #define NEXUS_IMPL_STATIC
#else
    #define NEXUS_IMPL_STATIC static
#endif
// use static asserts by default only under c11 and above
// Do not use static asserts for the frama-c build
#ifndef NEXUS_STATIC_ASSERT
    #ifndef __FRAMAC__
        #if (defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L))
            #define NEXUS_STATIC_ASSERT(b, m) _Static_assert(b, m)
        #elif (defined(__cplusplus) && (__cplusplus >= 201103L))
            #include <assert.h>
            #define NEXUS_STATIC_ASSERT(b, m) static_assert(b, m)
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

// Intentional 'unused' macro
#define NEXUS_UNUSED(x) (void) (x)

#endif /* ifndef NEXUS__SRC__INTERNAL_COMMON_CONFIG_H_ */
