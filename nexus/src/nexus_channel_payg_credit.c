/** \file
 * Nexus Channel PAYG Credit Module (Implementation)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "src/nexus_channel_payg_credit.h"

#if NEXUS_CHANNEL_ENABLED

void nexus_channel_payg_credit_init(void)
{
    return;
}

void nexus_channel_payg_credit_get(oc_request_t* request,
                                   oc_interface_mask_t if_mask,
                                   void* data)
{
    PRINT("stubbed out for now\n");
    (void) request;
    (void) if_mask;
    (void) data;
    return;
}

#endif /* NEXUS_CHANNEL_ENABLED */
