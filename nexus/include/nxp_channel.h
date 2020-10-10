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

#ifdef __cplusplus
extern "C" {
#endif

/** Return device-specific unique 16-byte authentication key.
 *
 * Return a copy of the device-unique secret key to use for channel origin
 * command authentication. The secret key must not change over the lifecycle
 * of the device.
 *
 * It is recommended that this is a different key from the 'keycode'
 * symmetric key, but if NV storage is limited, it is possible to generate a
 * different (but consistent) key 'on the fly' by passing the secret keycode
 * key through a symmetric one-way hashing function.
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
 * Often, this information might be used to update a UI display (for
 * example, if a link handshake has completed successfully).
 *
 * \param event enum indicating the event which occurred
 */
void nxp_channel_notify_event(enum nxp_channel_event_type event);

/*! \brief Send outbound Nexus Channel application data packet
 *
 * Send data from the Nexus Channel system running on this device to the
 * network hardware (dependent on the implementing product).
 *
 * The `source` is fixed, and is the Nexus ID address of *this*
 * device. `dest` is the Nexus ID of the device to
 * receive the payload.
 *
 * This function provides an `is_multicast` convenience flag, indicating
 * whether the message should be transmitted to *all* connected devices, or
 * *one*
 * connected device on the local network. If `is_multicast` is true, the
 * `dest` nx_id will be: {authority_id = 0xFF00, device_id = 158}. 158 is
 * selected to map to the "All OCF Nodes" IANA IPV6 address here:
 * (https://www.iana.org/assignments/ipv6-multicast-addresses/ipv6-multicast-addresses.xhtml)
 *
 * \param bytes_to_send pointer to application data to send
 * \param bytes_count number of valid bytes at `bytes_to_send`
 * \param source Nexus ID of the sending device (this device)
 * \param dest Nexus ID of the destination device (other device)
 * \param is_multicast if true, send message to all devices
 * \return `nx_channel_error` indicating success or failure (and cause)
 */
nx_channel_error nxp_channel_network_send(const void* const bytes_to_send,
                                          uint32_t bytes_count,
                                          const struct nx_id* const source,
                                          const struct nx_id* const dest,
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

#ifdef CONFIG_NEXUS_CHANNEL_USE_PAYG_CREDIT_RESOURCE

/* Update the remaining PAYG credit on this device.
 *
 * This function is called when this device is operating in a 'dependent'
 * PAYG credit mode, and another (authorized) Nexus Channel device updates
 * the credit on this device.
 *
 * If '0', the device should be functionally disabled. If any value other
 * than 0, the remaining amount of PAYG credit on the device should be
 * updated to that value.
 *
 * Units are determined at compile-time, but are assumed to be 'seconds' if
 * not otherwise specified.
 *
 * See also `nxp_channel_payg_credit_unlock`.
 *
 * \param remaining amount of credit (in `units`) which this device now has
 * \return `nx_channel_error` indicating success or failure (and cause)
 */
nx_channel_error nxp_channel_payg_credit_set(uint32_t remaining);

/* Remove PAYG restrictions from this device.
 *
 * This function is called when this device is operating in a 'dependent'
 * PAYG credit mode, and another (authorized) Nexus Channel device
 * permanently unlocks this PAYG device.
 *
 * After receiving this command, the device should be able to be used
 * indefinitely, until a subsequent `payg_credit_update` command is received
 * to set the credit to a value other than 'unlocked'.
 * * See also `nxp_channel_payg_credit_update`.
 *
 * \return `nx_channel_error` indicating success or failure (and cause)
 */
nx_channel_error nxp_channel_payg_credit_unlock(void);

#endif // #ifdef CONFIG_NEXUS_CHANNEL_USE_PAYG_CREDIT_RESOURCE

#ifdef __cplusplus
}
#endif

#endif /* end of include guard: NEXUS__INC__NXP_CHANNEL_H_ */
