/** \file nexus_oc_wrapper.h
 * Nexus-OC Wrapper Module (Header)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#ifndef NEXUS__SRC__NEXUS_OC_WRAPPER_INTERNAL_H_
#define NEXUS__SRC__NEXUS_OC_WRAPPER_INTERNAL_H_

#include "src/nexus_channel_core.h"

#if NEXUS_CHANNEL_ENABLED

#include <stdbool.h>
#include <stdint.h>

#include "oc/include/oc_ri.h"

// "All OCF Nodes" FF0X:0:0:0:0:0:0:158
extern oc_endpoint_t NEXUS_OC_WRAPPER_MULTICAST_OC_ENDPOINT_T_ADDR;

/** Wrapper for `oc_add_resource` method.
 *
 * Thin wrapper for Nexus includes the following changes:
 *
 * * don't allow registration to a URI that's already been registered
 *
 * \param resource the resource to add to the Nexus application
 * \return true if the resource could be added
 */
bool nexus_add_resource(oc_resource_t* resource);

/** Wrapper for `oc_resource_set_request_handler` method.
 *
 * Thin wrapper for Nexus includes the following changes:
 *
 * * removes unused `user_data` parameter
 * * prevents registration if a handler is already registered to this resource
 *   for the requested method
 *
 * \param res pointer to the `oc_resource_t` to register the handler
 * \param method method (`oc_method_t`) to register the handler to
 * \param handler pointer to the handler function to register
 * \return true if the registration was succesful; false otherwise
 */
bool nexus_resource_set_request_handler(oc_resource_t* resource,
                                        oc_method_t method,
                                        oc_request_callback_t callback);

/** Convenience to convert an OC endpoint IPV6 address to Nexus IPV6.
 *
 * \param source_endpoint oc_endpoint_t to convert
 * \param dest_nx_ipv6_address output for nx_ipv6_address format
 */
void nexus_oc_wrapper_oc_endpoint_to_nx_ipv6(
    const oc_endpoint_t* const source_endpoint,
    struct nx_ipv6_address* dest_nx_ipv6_address);

/** Convenience to convert an OC endpoint IPV6 address to Nexus IPV6.
 *
 * \param source_endpoint oc_endpoint_t to convert
 * \param dest_nx_ipv6_address output for nx_ipv6_address format
 */
bool nexus_oc_wrapper_oc_endpoint_to_nx_id(
    const oc_endpoint_t* const source_endpoint, struct nx_id* dest_nx_id);

#endif /* NEXUS_CHANNEL_ENABLED */
#endif /* ifndef NEXUS__SRC__NEXUS_OC_WRAPPER_INTERNAL_H_ */
