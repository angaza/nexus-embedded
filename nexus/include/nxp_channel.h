/** \file nxp_channel.h
 * \brief Platform interface required by the Nexus Channel library.
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 *
 * Contains declarations of functions, enums, and structs that the Nexus
 * Keycode library uses to interface with port resources (the resources of
 * The platform that is using the library).
 *
 * All port interfaces are included in this single header. Implementation
 * is necessarily platform-specific and must be completed by the manufacturer.
 */

#ifndef NEXUS__INC__NXP_CHANNEL_H_
#define NEXUS__INC__NXP_CHANNEL_H_

#include "include/nx_channel.h"
#include "include/nx_core.h"
#include <stdbool.h>

/** Return device-specific unique 16-byte authentication key.
 *
 * Return a copy of the device-unique secret key to use for channel origin
 * command authentication. The secret key must not change over the lifecycle of
 * the device.
 *
 * It is recommended that this is a different key from the 'keycode' symmetric
 * key, but if NV storage is limited, it is possible to generate a different
 * (but consistent) key 'on the fly' by passing the secret keycode key
 * through a symmetric one-way hashing function.
 *
 * \return copy of permanent, 16-byte device-specific secret key
 */
struct nx_core_check_key nxp_channel_symmetric_origin_key(void);

/** Name of a specific Nexus Channel event type.
 */
enum nxp_channel_event_type
{
    /* Successfully set up a link with this device as accessory.
     */
    NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_ACCESSORY,
    /* Successfully set up a link with this device as controller.
     */
    NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_CONTROLLER,
    /* A link was deleted (due to timeout or manual intervention).
     */
    NXP_CHANNEL_EVENT_LINK_DELETED,
    /* A link handshake has begun and is now in progress.
     */
    NXP_CHANNEL_EVENT_LINK_HANDSHAKE_STARTED,
    /* A link handshake has timed out. The alternative is for
     * a link handshake to end in a
     * `NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_ACCESSORY` or
     * `NXP_CHANNEL_EVENT_LINK_ESTABLISHED_AS_CONTROLLER` event.
     */
    NXP_CHANNEL_EVENT_LINK_HANDSHAKE_TIMED_OUT,
};

/*! \brief Be notified of events in the Nexus Channel system
 *
 * This function is called by Nexus Channel to indicate that an event
 * has occurred which the implementing product may wish to act upon.
 * Often, this information might be used to update a UI display (for example,
 * if a link handshake has completed successfully).
 *
 * \param event enum indicating the event which occurred
 * \return void
 */
void nxp_channel_notify_event(enum nxp_channel_event_type event);

/*! \brief Send outbound Nexus Channel application data packet
 *
 * Send data from the Nexus Channel system running on this device to the
 * network hardware (dependent on the implementing product).
 *
 * This function provides an `is_multicast` flag, indicating whether the
 * message should be transmitted to *all* connected devices, or *one*
 * connected device on the local network.
 *
 * The `source_address` is fixed, and is the Nexus IPV6 address of *this*
 * device. The `dest_address` is the Nexus IPV6 address of the device to
 * receive this application payload. In the case of multicast devices, this
 * is an address registered with IANA for "All OCF Devices"
 * (https://www.iana.org/assignments/ipv6-multicast-addresses/ipv6-multicast-addresses.xhtml)
 *
 * \param bytes_to_send pointer to application data to send
 * \param bytes_count number of valid bytes at `bytes_to_send`
 * \param source_address Nexus IPV6 address of the sending device (this device)
 * \param dest_address Nexus IPV6 address which should receive this message
 * \param is_multicast True if message should be sent to *all* connected devices
 * \return `nx_channel_error` indicating success or failure (and cause)
 */
nx_channel_error
nxp_channel_network_send(const void* const bytes_to_send,
                         uint32_t bytes_count,
                         const struct nx_ipv6_address* const source_address,
                         const struct nx_ipv6_address* const dest_address,
                         bool is_multicast);

/* Retrieve the value of the Nexus ID of this device.
 *
 * A Nexus ID is unique to each Nexus device (globally), and does not
 * necessarily match to the user-facing "PAYG ID". The Nexus ID is used to
 * uniquely identify different Nexus Channel devices.
 *
 * A nexus ID is persisted in device firmware in the factory, never changes
 * over the life of the product, and is registered in the Nexus API as a
 * globally valid Nexus ID.
 *
 * \return copy of the permanent Nexus ID of this device
 */
struct nx_id nxp_channel_get_nexus_id(void);

#endif /* end of include guard: NEXUS__INC__NXP_CHANNEL_H_ */
