/** \file
 * Nexus Channel Shared Configuration Parameters
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

// These parameters are needed to configure IoTivity core and Nexus Channel.
// This file is unlikely to be used by the implementing product, and can be
// ignored (but not deleted or moved...)

#ifndef NEXUS__CHANNEL__INCLUDE__SHARED_OC_CONFIG_H_
#define NEXUS__CHANNEL__INCLUDE__SHARED_OC_CONFIG_H_

#include "include/user_config.h"
#include <stdint.h>

// In most cases, there is no need to modify the values of this file.
// Convert user configuration to internal config.
// 'CONFIG' prefix from Kconfig
#ifndef CONFIG_NEXUS_CHANNEL_ENABLED
// Used internally to determine whether to compile in channel features
#define NEXUS_CHANNEL_ENABLED 0
#else
#define NEXUS_CHANNEL_ENABLED 1
#endif

// Compile-time parameter checks
#ifndef NEXUS_CHANNEL_ENABLED
#error "NEXUS_CHANNEL_ENABLED must be defined."
#endif

// Set up further configuration parameters
#if NEXUS_CHANNEL_ENABLED

// both controllers and accessories may act in client or server roles
// depending on resource in question
#define OC_CLIENT 1
#define OC_SERVER 1

#if defined(CONFIG_NEXUS_CHANNEL_PLATFORM_CONTROLLER_MODE_SUPPORTED)
#define NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE 1
#define NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE 0

#elif defined(CONFIG_NEXUS_CHANNEL_PLATFORM_ACCESSORY_MODE_SUPPORTED)
#define NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE 0
#define NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE 1

#elif defined(CONFIG_NEXUS_CHANNEL_PLATFORM_DUAL_MODE_SUPPORTED)
#define NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE 1
#define NEXUS_CHANNEL_SUPPORT_ACCESSORY_MODE 1
#endif /* if defined(CONFIG_NEXUS_CHANNEL_PLATFORM_...) */

// Is int64/uint64 supported?
#ifndef UINT64_MAX
#error "Nexus Channel requires uint64_t support."
#endif
#ifndef INT64_MAX
#error "Nexus Channel requires int64_t support."
#endif

// if Nexus Channel is enabled, then Channel Security is by default enabled;
// this could be a configurable option in the future
#define NEXUS_CHANNEL_LINK_SECURITY_ENABLED 1

// Used to conditionally include internal PAYG credit resource
#if defined(CONFIG_NEXUS_CHANNEL_USE_PAYG_CREDIT_RESOURCE)
#define NEXUS_CHANNEL_USE_PAYG_CREDIT_RESOURCE 1
#else
#define NEXUS_CHANNEL_USE_PAYG_CREDIT_RESOURCE 0
#endif

#else

#define NEXUS_CHANNEL_LINK_SECURITY_ENABLED 0
// No OC client or server roles if Channel is not in use
#define OC_CLIENT 0
#define OC_SERVER 0

#endif /* if NEXUS_CHANNEL_ENABLED */

#endif /* ifndef NEXUS__CHANNEL__INCLUDE__SHARED_OC_CONFIG_H_ */
