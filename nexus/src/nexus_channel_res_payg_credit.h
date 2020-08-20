/** \file
 * Nexus Channel PAYG Credit OCF Resource (Header)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all
 * copies or substantial portions of the Software.
 *
 * Explanation of operational modes:
 *
 *                     I EXPECT CREDIT UPDATES FROM
                       A (LINKED) CONTROLLER

                           TRUE           FALSE
                    +---------------+--------------+
                  T |               |              |
                  R |               |              |
                  U |    RELAYING   |    LEADING   |
I UPDATE THE      E |               |              |
CREDIT OF MY        |               |              |
(LINKED)            +------------------------------+
ACCESSORIES       F |               |              |
                  A |               |              |
                  L |   FOLLOWING   | DISCONNECTED |
                  S |               |              |
                  E |               |              |
                    +---------------+--------------+



                       I AM LINKED AS AN ACCESSORY


                           TRUE           FALSE
                    +---------------+--------------+
                  T |               |              |
                  R |               |              |
                  U |    RELAYING   |    LEADING   |
I AM LINKED AS    E |               |              |
  A CONTROLLER      |               |              |
                    +------------------------------+
                  F |               |              |
                  A |               |              |
                  L |   FOLLOWING   | DISCONNECTED |
                  S |               |              |
                  E |               |              |
                    +---------------+--------------+
 */
#ifndef NEXUS__CHANNEL__SRC__NEXUS_CHANNEL_RES_PAYG_CREDIT__H
#define NEXUS__CHANNEL__SRC__NEXUS_CHANNEL_RES_PAYG_CREDIT__H

#include "src/internal_channel_config.h"

#if NEXUS_CHANNEL_ENABLED
#if NEXUS_CHANNEL_USE_PAYG_CREDIT_RESOURCE

// Exposed only for unit tests to confirm resource model contents
#ifdef NEXUS_DEFINED_DURING_TESTING
extern const char* PAYG_CREDIT_REMAINING_SHORT_PROP_NAME;
extern const char* PAYG_CREDIT_UNITS_SHORT_PROP_NAME;
extern const char* PAYG_CREDIT_MODE_SHORT_PROP_NAME;
extern const char* PAYG_CREDIT_SUPPORTED_MODES_SHORT_PROP_NAME;

// expose internal state for unit tests
uint32_t _nexus_channel_payg_credit_remaining_credit(void);
#endif

enum nexus_channel_payg_credit_operating_mode
{
    NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_DISCONNECTED = 0,
    NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_LEADING = 1,
    NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_FOLLOWING = 2,
    NEXUS_CHANNEL_PAYG_CREDIT_OPERATING_MODE_RELAYING = 3,
};

/* Initialize the Nexus Channel PAYG Credit module.
 *
 * Called on startup by `nexus_channel_core_init()`.
 *
 * \return void
 */
void nexus_channel_res_payg_credit_init(void);

#ifdef NEXUS_INTERNAL_IMPL_NON_STATIC

void nexus_channel_res_payg_credit_get_handler(oc_request_t* request,
                                               oc_interface_mask_t interfaces,
                                               void* user_data);

void nexus_channel_res_payg_credit_post_handler(oc_request_t* request,
                                                oc_interface_mask_t interfaces,
                                                void* user_data);

enum nexus_channel_payg_credit_operating_mode
_nexus_channel_res_payg_credit_get_credit_operating_mode(void);

#endif // NEXUS_INTERNAL_IMPL_NON_STATIC

#endif // NEXUS_CHANNEL_USE_PAYG_CREDIT_RESOURCE
#endif // NEXUS_CHANNEL_ENABLED

#endif // NEXUS__CHANNEL__SRC__NEXUS_CHANNEL_RES_PAYG_CREDIT__H
