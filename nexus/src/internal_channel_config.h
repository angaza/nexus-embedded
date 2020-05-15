/** \file
 * Nexus Channel Protocol Internal Configuration Parameters
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#ifndef NEXUS__CHANNEL__SRC__INTERNAL_CHANNEL_CONFIG_H_
#define NEXUS__CHANNEL__SRC__INTERNAL_CHANNEL_CONFIG_H_

// "nxp" will be included independently by modules that them.
#include "include/nx_channel.h"
#include "src/internal_common_config.h"
#include <stdbool.h>

#include "oc/include/oc_client_state.h"
#include "oc/include/oc_ri.h"

// Compile-time parameter checks
#ifndef NEXUS_CHANNEL_ENABLED
#error "NEXUS_CHANNEL_ENABLED must be defined"
#endif

// Set up further configuration parameters
#if NEXUS_CHANNEL_ENABLED

// Identifies the Nexus channel protocol public 'release version'.
#define NEXUS_CHANNEL_PROTOCOL_RELEASE_VERSION_COUNT 1

// Not externally exposed, configures number of link handshakes a controller
// can simultaneously have.
#define NEXUS_CHANNEL_SIMULTANEOUS_LINK_HANDSHAKES 4

// maximum number of simultaneous Nexus Channel links that can be established
// Once reaching this limit, devices must be unlinked to link more devices.
// Increasing increases RAM and NV use.
#define NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS                                   \
    CONFIG_NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS

// Seconds that an established link must be idle before being deleted
// from this system. 7776000 = 3 months
#define NEXUS_CHANNEL_LINK_TIMEOUT_SECONDS 7776000

/*
 * Possible ways to secure communication on a Nexus Channel Link.
 *
 * Used by Link Handshake manager and Link manager to set up a new link
 * and manage encryption and authentication on an existing link, respectively.
 */
enum nexus_channel_link_security_mode
{
    // No encryption, 128-bit symmetric key, COSE MAC0 computed w/ Siphash 2-4
    NEXUS_CHANNEL_LINK_SECURITY_MODE_KEY128SYM_COSE_MAC0_AUTH_SIPHASH24 = 0,
    // 1-3 reserved
};

/*
 * Possible operating modes for a device on Nexus Channel Link.
 *
 * Typically, a device on one end of the link is a 'controller', and the
 * other is an 'accessory'.
 */
enum nexus_channel_link_operating_mode
{
    // operating as an accessory only
    CHANNEL_LINK_OPERATING_MODE_ACCESSORY = 0,
    // operating as a controller only
    CHANNEL_LINK_OPERATING_MODE_CONTROLLER = 1,
    // simultaneous accessory and controller modes
    CHANNEL_LINK_OPERATING_MODE_DUAL_MODE = 2,
};

#endif /* NEXUS_CHANNEL_ENABLED */
#endif /* ifndef NEXUS__CHANNEL__SRC__INTERNAL_CHANNEL_CONFIG_H_ */
