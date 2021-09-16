// Modifications (c) 2020 Angaza, Inc.
// based on https://github.com/iotivity/iotivity-lite/blob/master/port/linux/oc_config.h

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcomment"

#ifndef OC_CONFIG_H
#define OC_CONFIG_H

/* Time resolution */
#include <stdint.h>
// Don't rely on time.h, not available on embedded platforms
//#include <time.h>

// For shared Nexus Channel propagated includes
#include "include/shared_oc_config.h"

#ifndef OC_CLIENT
#error "OC_CLIENT must be defined"
#endif
#ifndef OC_SERVER
#error "OC_SERVER must be defined"
#endif

#ifdef __cplusplus
extern "C"
{
#endif

typedef uint64_t oc_clock_time_t;

// In future, may be able to remove entirely
#define OC_CLOCK_CONF_TICKS_PER_SECOND NEXUS_OC_CLOCKS_PER_SEC

//#define OC_SPEC_VER_OIC
// Security Layer
// Max inactivity timeout before tearing down DTLS connection
/*
#define OC_DTLS_INACTIVITY_TIMEOUT (600)

// Maximum wait time for select function
#define SELECT_TIMEOUT_SEC (1)

// Add support for passing network up/down events to the app
#define OC_NETWORK_MONITOR
// Add support for passing TCP/TLS/DTLS session connection events to the app
#define OC_SESSION_EVENTS

// Add support for software update
//#define OC_SOFTWARE_UPDATE or run "make" with SWUPDATE=1
// Add support for the oic.if.create interface in Collections
//#define OC_COLLECTIONS_IF_CREATE or run "make" with CREATE=1
// Add support for the maintenance resource
//#define OC_MNT or run "make" with MNT=1

// Add support for dns lookup to the endpoint
#define OC_DNS_LOOKUP
#define OC_DNS_LOOKUP_IPV6

// If we selected support for dynamic memory allocation
#ifdef OC_DYNAMIC_ALLOCATION
#define OC_COLLECTIONS
#define OC_BLOCK_WISE

#else // OC_DYNAMIC_ALLOCATION
/* List of constraints below for a build that does not employ dynamic
   memory allocation
*/
// Memory pool sizes
#define OC_BYTES_POOL_SIZE (512)
#define OC_INTS_POOL_SIZE (50)
#if NEXUS_CHANNEL_OC_SUPPORT_DOUBLES
    #define OC_DOUBLES_POOL_SIZE (4)
#endif
/*
// Server-side parameters
// Maximum number of server resources
// XXX: May expose via Kconfig to allow for more resources in cases where
// required.
*/
#define OC_MAX_APP_RESOURCES (4)
/*
#define OC_MAX_NUM_COLLECTIONS (1)

// Common parameters
// Prescriptive lower layers MTU size, enable block-wise transfers
#define OC_BLOCK_WISE_SET_MTU (700)
*/

// Maximum number of concurrent requests
#define OC_MAX_NUM_CONCURRENT_REQUESTS (2)

// Maximum number of nodes in a payload tree structure
#define OC_MAX_NUM_REP_OBJECTS (25)

// Number of devices on the OCF platform
#define OC_MAX_NUM_DEVICES (1)

#ifdef __cplusplus
}
#endif

#pragma GCC diagnostic pop
#endif /* OC_CONFIG_H */
