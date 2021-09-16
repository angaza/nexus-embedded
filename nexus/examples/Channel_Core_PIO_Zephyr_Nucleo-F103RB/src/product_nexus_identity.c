/** \file product_nexus_identity.c
 * \brief Identity and Cryptographic Key Management
 * \author Angaza
 * \copyright 2021 Angaza, Inc
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "product_nexus_identity.h"
// Zephyr modules for NV access
#include "flash_filesystem.h"
#include <assert.h>
// for memset, memcpy
#include <string.h>

const struct nx_id PRODUCT_NEXUS_IDENTITY_DEFAULT_NEXUS_ID = {0xFFFF,
                                                              0xFFFFFFFF};
const struct nx_common_check_key
    PRODUCT_NEXUS_IDENTITY_DEFAULT_KEYCODE_SECRET_KEY = {{0xFF,
                                                          0xFF,
                                                          0xFF,
                                                          0xFF,
                                                          0xFF,
                                                          0xFF,
                                                          0xFF,
                                                          0XFF,
                                                          0xFF,
                                                          0xFF,
                                                          0xFF,
                                                          0xFF,
                                                          0xFF,
                                                          0xFF,
                                                          0xFF,
                                                          0XFF}};
const struct nx_common_check_key
    PRODUCT_NEXUS_IDENTITY_DEFAULT_CHANNEL_SECRET_KEY = {{0xFF,
                                                          0xFF,
                                                          0xFF,
                                                          0xFF,
                                                          0xFF,
                                                          0xFF,
                                                          0xFF,
                                                          0XFF,
                                                          0xFF,
                                                          0xFF,
                                                          0xFF,
                                                          0xFF,
                                                          0xFF,
                                                          0xFF,
                                                          0xFF,
                                                          0XFF}};

// RAM copies of the device IDs and secret keys.
// Security Note: There is a security risk here in that examining the RAM
// once these are loaded will show the security keys. However, not
// all hardware has an MPU to protect access against RAM reads. If implementing
// on a system with an MPU, recommend storing this data in a RAM address that
// is protected against unauthorized reads (resulting in an MPU fault).
static struct
{
    struct nx_id my_nexus_id;
    struct nx_common_check_key my_channel_secret_key;
    struct nx_common_check_key my_keycode_secret_key;
} _this;

void product_nexus_identity_set_nexus_id(const struct nx_id* const id)
{
    // Note: `nx_id` is a packed struct, so no further
    // alignment is required.
    const int bytes_written = flash_filesystem_write_product_nv(
        FLASH_FILESYSTEM_PRODUCT_NV_ID_NEXUS_ID,
        id,
        sizeof(struct nx_id)); // writes 6 bytes
    assert(bytes_written == sizeof(struct_nx_id));
    memcpy(&_this.my_nexus_id, id, sizeof(struct nx_id));
}

void product_nexus_identity_set_nexus_keycode_secret_key(
    const struct nx_common_check_key* const keycode_key)
{
    // Note: `nx_id` is a packed struct, so no further
    // alignment is required.
    const int bytes_written = flash_filesystem_write_product_nv(
        FLASH_FILESYSTEM_PRODUCT_NV_ID_NEXUS_KEYCODE_SECRET_KEY,
        keycode_key,
        sizeof(struct nx_common_check_key)); // writes 16 bytes
    assert(bytes_written == sizeof(struct nx_common_check_key));
    memcpy(&_this.my_keycode_secret_key,
           keycode_key,
           sizeof(struct nx_common_check_key));
}

void product_nexus_identity_set_nexus_channel_secret_key(
    const struct nx_common_check_key* const channel_key)
{
    // Note: `nx_id` is a packed struct, so no further
    // alignment is required.
    const int bytes_written = flash_filesystem_write_product_nv(
        FLASH_FILESYSTEM_PRODUCT_NV_ID_NEXUS_CHANNEL_SECRET_KEY,
        channel_key,
        sizeof(struct nx_common_check_key)); // writes 16 bytes
    assert(bytes_written == sizeof(struct nx_common_check_key));
    memcpy(&_this.my_channel_secret_key,
           channel_key,
           sizeof(struct nx_common_check_key));
}

struct nx_id* product_nexus_identity_get_nexus_id(void)
{
    const int bytes_read = flash_filesystem_read_product_nv(
        FLASH_FILESYSTEM_PRODUCT_NV_ID_NEXUS_ID,
        &_this.my_nexus_id,
        sizeof(struct nx_id));

    if (bytes_read != sizeof(struct nx_id))
    {
        // No NV copy of the Nexus ID, set it to the default value
        memcpy(&_this.my_nexus_id,
               &PRODUCT_NEXUS_IDENTITY_DEFAULT_NEXUS_ID,
               sizeof(_this.my_nexus_id));
    }
    return &_this.my_nexus_id;
}

struct nx_common_check_key*
product_nexus_identity_get_nexus_keycode_secret_key(void)
{
    const int bytes_read = flash_filesystem_read_product_nv(
        FLASH_FILESYSTEM_PRODUCT_NV_ID_NEXUS_KEYCODE_SECRET_KEY,
        &_this.my_channel_secret_key,
        sizeof(struct nx_common_check_key));
    if (bytes_read != sizeof(struct nx_common_check_key))
    {
        // No NV copy of the keycode secret key, set it to the default value
        memcpy(_this.my_keycode_secret_key.bytes,
               &PRODUCT_NEXUS_IDENTITY_DEFAULT_KEYCODE_SECRET_KEY,
               sizeof(struct nx_common_check_key));
    }

    return &_this.my_keycode_secret_key;
}

struct nx_common_check_key*
product_nexus_identity_get_nexus_channel_secret_key(void)
{
    const int bytes_read = flash_filesystem_read_product_nv(
        FLASH_FILESYSTEM_PRODUCT_NV_ID_NEXUS_CHANNEL_SECRET_KEY,
        &_this.my_channel_secret_key,
        sizeof(struct nx_common_check_key));
    if (bytes_read != sizeof(struct nx_common_check_key))
    {
        // No NV copy of the channel secret key, set it to the default value
        memcpy(_this.my_keycode_secret_key.bytes,
               &PRODUCT_NEXUS_IDENTITY_DEFAULT_CHANNEL_SECRET_KEY,
               sizeof(struct nx_common_check_key));
    }

    return &_this.my_channel_secret_key;
}