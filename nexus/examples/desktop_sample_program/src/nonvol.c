/** \file nonvol.c
 * \brief A mock implementation of nonvolatile storage for POSIX filesystems.
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "identity.h"
#include "nx_keycode.h"
#include "nxp_core.h"
#include "payg_state.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>

/**
 * NOTE: This implementation assumes a POSIX compliant file system. Embedded
 * applications will require a platform-specific implementation.
 */

#define BLOCK_SENTINEL_NX 'n' // indicates Nexus Keycode NV blocks
#define BLOCK_SENTINEL_PROD 'p' // indicates product NV blocks

#define PROD_PAYG_STATE_BLOCK_ID 0
#define PROD_IDENTITY_BLOCK_ID 1

static struct
{
    char nv_file_path[100];
} _this;

bool nv_init(void)
{
    // Prompt for the location to read/write data to.
    printf("Please enter the path to the NV file (if it does not exist, then "
           "it will be created).\n");

    scanf("%s", &_this.nv_file_path[0]);

    // Ensure the file exists before first read.
    FILE* fptr = NULL;
    fptr = fopen(_this.nv_file_path, "ab");
    if (fptr == NULL)
    {
        // Unable to open the file; exit the program.
        perror(
            "Unable to find or create file at the specified path. Closing.\n");
        return false;
    }

    fclose(fptr);

    return true;
}

void _unlock_and_close_file(FILE* fptr)
{
    assert(flock(fileno(fptr), LOCK_UN) == 0);
    fclose(fptr);
}

bool nv_write(char block_sentinel,
              uint16_t block_id,
              uint8_t block_length,
              void* write_buffer)
{
    // Append NV blocks to the end of the file. This way failed writes will not
    // corrupt stable data.
    FILE* fptr = NULL;
    fptr = fopen(_this.nv_file_path, "ab");

    // Should always be able to open the file that was previously specified.
    if (fptr == NULL)
    {
        return false;
    }

    // Place an exclusive lock on this file while writing; no other process
    // should be reading or writing to it at this time.
    if (flock(fileno(fptr), LOCK_EX) != 0)
    {
        _unlock_and_close_file(fptr);
        return false;
    }

    // Write block sentinel.
    if (fputc(block_sentinel, fptr) != block_sentinel)
    {
        _unlock_and_close_file(fptr);
        return false;
    }

    // Write block ID.
    fwrite(&block_id, sizeof(uint16_t), 1, fptr);
    if (ferror(fptr) != 0)
    {
        _unlock_and_close_file(fptr);
        return false;
    }

    // Write block length.
    fwrite(&block_length, sizeof(uint8_t), 1, fptr);
    if (ferror(fptr) != 0)
    {
        _unlock_and_close_file(fptr);
        return false;
    }

    // Write block data.
    int write_count =
        (int) fwrite(write_buffer, sizeof(uint8_t), block_length, fptr);
    if (ferror(fptr) != 0 || write_count != block_length)
    {
        _unlock_and_close_file(fptr);
        return false;
    }

    // asserts and will fail if unlock failed
    _unlock_and_close_file(fptr);

    return true;
}

bool nxp_core_nv_write(const struct nx_core_nv_block_meta block_meta,
                       void* write_buffer)
{
    return nv_write(BLOCK_SENTINEL_NX,
                    block_meta.block_id,
                    block_meta.length,
                    write_buffer);
}

bool prod_nv_write_identity(uint8_t length, void* write_buffer)
{
    return nv_write(
        BLOCK_SENTINEL_PROD, PROD_IDENTITY_BLOCK_ID, length, write_buffer);
}

bool prod_nv_write_payg_state(uint8_t length, void* write_buffer)
{
    return nv_write(
        BLOCK_SENTINEL_PROD, PROD_PAYG_STATE_BLOCK_ID, length, write_buffer);
}

bool nv_read(char block_sentinel,
             uint16_t block_id,
             uint8_t block_length,
             void* read_buffer)
{
    // Open the file containing NV information.
    FILE* fptr = NULL;
    fptr = fopen(_this.nv_file_path, "rb");

    // Should always be able to open the file that was previously specified.
    if (fptr == NULL)
    {
        return false;
    }

    // Place a shared lock on the file while reading; other processes may
    // also place locks.
    if (flock(fileno(fptr), LOCK_SH) != 0)
    {
        fclose(fptr);
        return false;
    }

    // Keep track of whether or not we found the block.
    bool success = false;
    while (feof(fptr) == 0)
    {
        uint16_t cur_block_id = 0;
        uint8_t cur_block_length = 1;

        // Read block sentinel.
        int cur_block_sentinel = fgetc(fptr);
        if (cur_block_sentinel == EOF)
        {
            break;
        }

        // Read block metadata.
        if (fread(&cur_block_id, sizeof(uint16_t), 1, fptr) != 1)
        {
            break;
        }

        if (fread(&cur_block_length, sizeof(uint8_t), 1, fptr) != 1)
        {
            break;
        }

        // Note that `malloc` is not an option on many embedded platforms.
        uint8_t* block = malloc(cur_block_length * sizeof(uint8_t));
        if (block == NULL)
        {
            break;
        }

        // Read block data.
        memset(block, 0, cur_block_length * sizeof(uint8_t));
        if (fread(block, sizeof(uint8_t), cur_block_length, fptr) !=
            cur_block_length)
        {
            break;
        }

        // Validate the block and check if the block ID is the one that was
        // requested.
        if ((char) cur_block_sentinel == block_sentinel &&
            cur_block_id == block_id && cur_block_length == block_length)
        {
            // We continue if we found the requested block or not as there may
            // be a more recent version of this block later in the file due to
            // appending in `port_nv_write`.
            success = true;
            memcpy(read_buffer, block, cur_block_length);
        }

        if (block != NULL)
        {
            free(block);
        }
    }

    // Safely close the file and release the lock.
    if (flock(fileno(fptr), LOCK_UN) != 0)
    {
        fclose(fptr);
        return false;
    }

    fclose(fptr);

    return success;
}

bool nxp_core_nv_read(const struct nx_core_nv_block_meta block_meta,
                      void* read_buffer)
{
    uint8_t intermediate_read_buffer[block_meta.length * sizeof(uint8_t)];

    if (nv_read(BLOCK_SENTINEL_NX,
                block_meta.block_id,
                block_meta.length,
                &intermediate_read_buffer) &&
        // If we got here, the block meta matches expected.
        nx_core_nv_block_valid(block_meta, intermediate_read_buffer))
    {
        memcpy(read_buffer, &intermediate_read_buffer, block_meta.length);
        return true;
    }
    else
    {
        return false;
    }
}

bool prod_nv_read_identity(uint8_t length, void* read_buffer)
{
    return nv_read(
        BLOCK_SENTINEL_PROD, PROD_IDENTITY_BLOCK_ID, length, read_buffer);
}

bool prod_nv_read_payg_state(uint8_t length, void* read_buffer)
{
    return nv_read(
        BLOCK_SENTINEL_PROD, PROD_PAYG_STATE_BLOCK_ID, length, read_buffer);
}
