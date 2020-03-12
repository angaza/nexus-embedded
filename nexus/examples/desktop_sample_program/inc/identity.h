/**
 * @file identity.h
 * @author Angaza
 * @date 25 February 2020
 * @brief File containing secret key management.
 *
 * This file is an example of the product-side code required to track device
 * secret keys for the Nexus Keycode library.
 */

#include "nxp_keycode.h"
#ifndef IDENTITY_H
#define IDENTITY_H

NEXUS_PACKED_STRUCT identity_struct
{
    uint32_t serial_id;
    struct nx_core_check_key secret_key;
};

#define PROD_IDENTITY_BLOCK_LENGTH sizeof(struct identity_struct)

/* @brief Initialize the identity module.
 *
 * This will prompt for a serial ID and a secret key.
 */
void identity_init(void);

#endif
