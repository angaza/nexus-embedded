/** \file product_nexus_identity.h
 * \brief Identity and Cryptographic Key Management
 * \author Angaza
 * \copyright 2021 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all
 * copies or substantial portions of the Software.
 *
 * Minimal example providing storage for secret keys and identity
 * information that would be provided to the device during factory
 * provisioning. These identities are the responsibility of the
 * manufacturer to provision and maintain, but are *used* by Nexus
 * Library in various instances.
 */
#ifndef PRODUCT_NEXUS_IDENTITY__H
#define PRODUCT_NEXUS_IDENTITY__H

#ifdef __cplusplus
extern "C" {
#endif

// To gain access to `nx_id` and `nx_common_check_key`
#include "nx_common.h"

/* Default Nexus ID used before the device has been factory provisioned
 */
extern const struct nx_id PRODUCT_NEXUS_IDENTITY_DEFAULT_NEXUS_ID;

/* Default keycode secret key to use before the device has been factory
 * provisioned
 */
extern const struct nx_common_check_key
    PRODUCT_NEXUS_IDENTITY_DEFAULT_KEYCODE_SECRET_KEY;

/* Default keycode secret key to use before the device has been factory
 * provisioned
 */
extern const struct nx_common_check_key
    PRODUCT_NEXUS_IDENTITY_DEFAULT_CHANNEL_SECRET_KEY;

/**
 * @brief identity_set_nexus_id
 *
 * Update the Nexus ID of this device.
 *
 * The Nexus ID has two parts - an 'authority_id' and
 * 'device_id'. For most devices, 'authority_id' is 0x0000, and
 * 'device_id' is the same as the PAYG ID of the device.
 *
 * The Nexus ID is typically written *once* into a device, at the factory.
 *
 * @param[in] id New Nexus ID of this device
 *
 * @return void
 */
void product_nexus_identity_set_nexus_id(const struct nx_id* const id);

/**
 * @brief identity_set_nexus_keycode_secret_key
 *
 * Update the Nexus *Keycode* secret key and persist to NV storage.
 *
 * This key is used to validate incoming Nexus Keycodes that are processed
 * by the device.
 *
 * The Keycode secret key is typically written *once* into a device, at the
 * factory.
 *
 * @param[in] keycode_key 16-byte symmetric secret key to persist
 *
 * @return void
 */
void product_nexus_identity_set_nexus_keycode_secret_key(
    const struct nx_common_check_key* const keycode_key);

/**
 * @brief identity_set_nexus_channel_secret_key
 *
 * Update the Nexus *Channel* secret key and persist to NV storage.
 *
 * This key is used to set up and verify Nexus Channel security links
 * with other devices. It may be different from the Keycode secret key.
 *
 * The Channel secret key is typically written *once* into a device, at the
 * factory.
 *
 * @param[in] channel_key 16-byte symmetric secret key to persist
 *
 * @return void
 */
void product_nexus_identity_set_nexus_channel_secret_key(
    const struct nx_common_check_key* const channel_key);

/**
 * @brief identity_get_nexus_id
 *
 * Retrieve the Nexus ID of this device.
 * Nexus ID is {0xFFFF, 0xFFFFFFFF} if not yet provisioned.
 *
 * @return Pointer to Nexus ID of this device
 */
struct nx_id* product_nexus_identity_get_nexus_id(void);

/**
 * @brief identity_get_nexus_keycode_secret_key
 *
 * Retrieve the Nexus Keycode secret key for this device.
 * Key is {0xFF * 16} if not yet provisioned.
 *
 * @return Pointer to Nexus Keycode secret key
 */
struct nx_common_check_key*
product_nexus_identity_get_nexus_keycode_secret_key(void);

/**
 * @brief identity_get_nexus_channel_secret_key
 *
 * Retrieve the Nexus Channel secret key for this device.
 * Key is {0xFF * 16} if not yet provisioned.
 *
 * @return Pointer to Nexus Channel secret key
 */
struct nx_common_check_key*
product_nexus_identity_get_nexus_channel_secret_key(void);

#ifdef __cplusplus
}
#endif
#endif // PRODUCT_NEXUS_IDENTITY__H
