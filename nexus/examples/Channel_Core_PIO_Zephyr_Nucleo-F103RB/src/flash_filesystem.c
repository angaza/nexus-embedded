/** \file flash_filesystem.c
 * \brief Example flash filesystem using Zephyr "NVS".
 * \author Angaza
 * \copyright 2021 Angaza, Inc
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 *
 * If not using Zephyr NVS for filesystem, replace the functionality here
 * with appropriate flash interfaces. This module is used by 'nxp_common.c' to
 * implement flash reads and writes required by Nexus.
 */

#include <assert.h>
// Zephyr modules for NV access
#include "flash_filesystem.h"
#include <drivers/flash.h>
#include <fs/nvs.h>
#include <storage/flash_map.h>

// struct representing persistent 'filesystem' in flash on this device
// This is a space in flash dedicated to NV reads/writes for the *Nexus
// library*, such as Nexus Channel link state or keycode history.
static struct nvs_fs nexus_filesystem;

// struct representing persistent 'filesystem' in flash on this device
// This is an area in flash dedicated to NV reads/writes for the (product side
// code*, such as PAYG credit or product-specific variables.
static struct nvs_fs product_filesystem;

static bool _filesystems_successfully_initialized = false;

// how many entries in flash to examine if the most recent entry is
// corrupted (such as if the most recent entry was a partially-completed flash
// write)
#define FLASH_FILESYSTEM_READ_NUMBER_OF_PAST_ENTRIES_TO_EXAMINE 10

bool flash_filesystem_init(void)
{
    if (_filesystems_successfully_initialized)
    {
        return true;
    }

    int result = 0;
    // Stores flash page info, which may vary between different SoCs
    struct flash_pages_info product_fp_info;
    struct flash_pages_info nexus_fp_info;

    // flash0 from DTS, see .pio/build/<build>/zephyr/zephyr.dts
    // and zephyr/stm32f103rb.overlay
    product_filesystem.offset = FLASH_AREA_OFFSET(product_nv_partition);
    nexus_filesystem.offset = FLASH_AREA_OFFSET(nexus_nv_partition);

    // Obtain *product NV* flash page info
    result = flash_get_page_info_by_offs(
        device_get_binding(DT_CHOSEN_ZEPHYR_FLASH_CONTROLLER_LABEL),
        product_filesystem.offset,
        &product_fp_info);

    if (result != 0)
    {
        // Unable to get flash page info, should not happen.
        assert(false);
        return false;
    }

    // Obtain *Nexus NV* flash page info
    result = flash_get_page_info_by_offs(
        device_get_binding(DT_CHOSEN_ZEPHYR_FLASH_CONTROLLER_LABEL),
        nexus_filesystem.offset,
        &nexus_fp_info);

    if (result != 0)
    {
        // Unable to get flash page info, should not happen.
        assert(false);
        return false;
    }
    product_filesystem.sector_size = product_fp_info.size;
    nexus_filesystem.sector_size = nexus_fp_info.size;

    // Since both flash pages are in the same peripheral, we expect
    // sector sizes to be the same (and assert 1024kB for the F103RB flash)
    assert(product_filesystem.sector_size == 1024);
    assert(nexus_filesystem.sector_size == 1024);

    // Dynamically calculate the number of sectors in the storage partition by
    // determining its size in bytes, and dividing by the size of each
    // sector/page.
    product_filesystem.sector_count =
        FLASH_AREA_SIZE(product_nv_partition) / product_fp_info.size;
    nexus_filesystem.sector_count =
        FLASH_AREA_SIZE(nexus_nv_partition) / nexus_fp_info.size;

    assert(product_filesystem.sector_count == 4);
    assert(nexus_filesystem.sector_count == 4);

    result =
        nvs_init(&product_filesystem, DT_CHOSEN_ZEPHYR_FLASH_CONTROLLER_LABEL);
    if (result != 0)
    {
        // unable to initialize, should not happen.
        assert(false);
        return false;
    }
    result =
        nvs_init(&nexus_filesystem, DT_CHOSEN_ZEPHYR_FLASH_CONTROLLER_LABEL);
    if (result != 0)
    {
        // unable to initialize, should not happen.
        assert(false);
        return false;
    }

    _filesystems_successfully_initialized = true;
    return true;
}

// Common code used by product and Nexus NV *write* functions
bool _internal_flash_filesystem_write_nv(struct nvs_fs* fs,
                                         uint16_t id,
                                         const void* data,
                                         size_t len)
{
    if (!_filesystems_successfully_initialized)
    {
        return false;
    }
    if (len < 1)
    {
        return false;
    }

    assert((fs == &nexus_filesystem) || (fs == &product_filesystem));
    const ssize_t bytes_written = nvs_write(fs, id, data, len);
    // bytes_written will be 0 if the same data already exists in flash
    return ((bytes_written == 0) || (bytes_written == len));
}

int flash_filesystem_write_product_nv(enum flash_filesystem_product_nv_id id,
                                      const void* data,
                                      size_t len)
{
    const bool success = _internal_flash_filesystem_write_nv(
        &product_filesystem, (uint16_t) id, data, len);
    if (success)
    {
        return len;
    }
    return 0;
}

int flash_filesystem_write_nexus_nv(uint16_t id, const void* data, size_t len)
{
    const bool success =
        _internal_flash_filesystem_write_nv(&nexus_filesystem, id, data, len);
    if (success)
    {
        return len;
    }
    return 0;
}

// Common code used by product and Nexus NV *read* functions
bool _internal_flash_filesystem_read_nv(struct nvs_fs* fs,
                                        uint16_t id,
                                        void* data,
                                        size_t len)
{
    if (!_filesystems_successfully_initialized)
    {
        return false;
    }
    if (len < 1)
    {
        return false;
    }

    assert((fs == &nexus_filesystem) || (fs == &product_filesystem));
    // if the most recent entry is corrupted, read up to 9 previous
    // entries in flash (if present)
    uint16_t writes_in_past = 0;
    ssize_t bytes_read = 0;
    while (writes_in_past <
           FLASH_FILESYSTEM_READ_NUMBER_OF_PAST_ENTRIES_TO_EXAMINE)
    {
        // Reading with writes_in_past == 0 is the same as calling `nvs_read`
        // (attempts to read most recent entry only). writes_in_past == 1
        // attempts to read one write before the most recent entry, etc.
        bytes_read = nvs_read_hist(fs, id, data, len, writes_in_past);
        if (bytes_read == len)
        {
            // return early, success.
            return true;
        }
        // nvs_read_hist
        else if (bytes_read == -ENOENT)
        {
            return false;
        }
        writes_in_past++;
    }
    // read 10 most recent flash entries and all were corrupted - return false.
    return false;
}

int flash_filesystem_read_product_nv(enum flash_filesystem_product_nv_id id,
                                     void* data,
                                     size_t len)
{
    const bool success = _internal_flash_filesystem_read_nv(
        &product_filesystem, (uint16_t) id, data, len);
    if (success)
    {
        return len;
    }
    return 0;
}

int flash_filesystem_read_nexus_nv(uint16_t id, void* data, size_t len)
{
    const bool success =
        _internal_flash_filesystem_read_nv(&nexus_filesystem, id, data, len);
    if (success)
    {
        return len;
    }
    return 0;
}
