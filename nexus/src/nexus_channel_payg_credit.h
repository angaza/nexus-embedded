/** \file
 * Nexus Channel PAYG Credit Module (Header)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#ifndef __NEXUS__CHANNEL__SRC__NEXUS_CHANNEL_PAYG_CREDIT_H_
#define __NEXUS__CHANNEL__SRC__NEXUS_CHANNEL_PAYG_CREDIT_H_

#include "src/internal_channel_config.h"

#if NEXUS_CHANNEL_ENABLED

#include "oc/include/oc_ri.h"
#include <stdbool.h>
#include <stdint.h>

/** Initialize the Nexus Channel PAYG Credit module.
 *
 * Called on startup by `nexus_channel_core_init`.
 */
void nexus_channel_payg_credit_init(void);

/** GET handler for incoming Nexus Channel requests.
 */
void nexus_channel_payg_credit_get(oc_request_t* request,
                                   oc_interface_mask_t if_mask,
                                   void* data);

#endif
#endif
