/** \file
 * Nexus Channel Battery Resource
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all
 * copies or substantial portions of the Software.
 *
 * Compliant implementation of the Nexus Channel Core
 * battery resource (rtr 101).
 * https://angaza.github.io/nexus-channel-models/resource_types/core/101-battery/redoc_wrapper.html
 * See also:
 * https://angaza.github.io/nexus-channel-models/resource_type_registry.html
 */
#ifndef EXAMPLE_BATTERY_RES__H
#define EXAMPLE_BATTERY_RES__H

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the battery resource.
 *
 * This will cause the battery resource to initialize
 * the values exposed by the resource to reasonable values,
 * and register the `GET` and `POST` handlers with Nexus Channel Core.
 */
void battery_res_init(void);

#ifdef __cplusplus
}
#endif

#endif // EXAMPLE_BATTERY_RES__H
