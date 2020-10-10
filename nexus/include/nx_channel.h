/** \file nx_channel.h
 * \brief Nexus Channel functions and structs shared by port and library code.
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 *
 * Contains declarations of functions, enums, and structs that the Nexus
 * Channel library uses to interface with port resources (the resources of
 * the platform that is using the library). The interface includes:
 *
 * * Registering Nexus Channel resources and resource method handlers
 * * Receiving Nexus Channel "Origin" commands
 *
 * All port interfaces are included in this single header. Implementation
 * is necessarily platform-specific and must be completed by the manufacturer.
 *
 */

#ifndef NEXUS__INC__NX_CHANNEL_H_
#define NEXUS__INC__NX_CHANNEL_H_

#include "nx_core.h"

#include "oc/include/oc_api.h"
#include "oc/include/oc_ri.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    NX_CHANNEL_ERROR_NONE = 0,
    NX_CHANNEL_ERROR_UNSPECIFIED = 1,
    NX_CHANNEL_ERROR_ACTION_REJECTED = 2,
    NX_CHANNEL_ERROR_METHOD_UNSUPPORTED = 3,
    NX_CHANNEL_ERROR_MESSAGE_TOO_LARGE = 10,
} nx_channel_error;

/** Nexus Channel resource initialization struct.
 *
 * Used in conjunction with `nx_channel_register_resource` to register
 * a new resource. Create an instance of this struct, set the values
 * appropriately, and then call `nx_channel_register_resource` to register
 * the resource instance in Nexus Channel Core.
 */
struct nx_channel_resource_props
{
    const char* uri; // C string representing the URI of this resource
    const char* resource_type; // full 'resource type' C string
    uint16_t
        rtr; // integer from
             // https://angaza.github.io/nexus-channel-models/resource_type_registry.html
    uint8_t num_interfaces; // number of values in `if_mask_arr`. Typically 1.
    const oc_interface_mask_t*
        if_masks; // Array of interface masks supported by this resource.
    oc_request_callback_t
        get_handler; // handler for GET requests (NULL if not implemented)
    bool get_secured; // true to secure GET method with Nexus Channel, false
                      // otherwise
    oc_request_callback_t
        post_handler; // handler for POST requests (NULL if not implemented)
    bool post_secured; // true to secure POST method with Nexus Channel, false
                       // otherwise
};

/** Register a new Nexus Channel resource.
 *
 * Resource registration allows Nexus Core to notify clients which resources
 * are available, e.g., GET to the discovery URI on "/nx/res".
 * (Note: Discovery is not implemented at this time).
 *
 * Requires at least one resource method handler to also be specified. To
 * add more method handlers after initial registration,
 * use `nx_channel_register_resource_handler`.
 *
 * NOTE: Only supports one resource type per resource
 * NOTE: Sets the *first* interface type in the input array `if_mask_arr` as
 * the default interface type
 * NOTE: Registers all resources to a single
 * Nexus *composite device* (see Sec 7.4 in
 * https://openconnectivity.org/specs/OCF_Device_Specification_v2.1.1.pdf)
 *
 * \param props pointer to struct with initialization properties
 * \return `nx_channel_error` detailing success or failure
 */
nx_channel_error
nx_channel_register_resource(const struct nx_channel_resource_props* props);

/** Register a method handler to an existing Nexus Channel resource.
 *
 * After resources are created (see `nx_channel_register_resource`), Nexus
 * Core can attach additional function handlers to resource requests, for
 * example, "GET /nx/pc". In this example, the handler for this request
 * would be implemented by the Nexus Channel PAYG Credit module. If
 * `secured` is true, then the resource method will only be accessible by
 * Nexus Channel nodes that have an active Nexus Channel Link with this
 * device.
 *
 * NOTE: Requires an existing resource at the specified URI to already exist
 *
 * \param uri C string representing the URI of the existing resource
 * \param method the method that the specified handler should be attached to
 * \param handler pointer to a function that will be called for the given
 * URI/method combination
 * \param secured true if the resource method should be secured by Nexus Channel
 * \return `nx_channel_error` detailing success or failure
 */
nx_channel_error
nx_channel_register_resource_handler(const char* uri,
                                     oc_method_t method,
                                     oc_request_callback_t handler,
                                     bool secured);

/*! \brief Nexus Channel origin command encoding/bearer type.
 *
 * The origin manager can receive 'origin commands' through various
 * protocols, this enum determines what 'protocol' of command is being
 * passed to the origin manager.
 */
enum nx_channel_origin_command_bearer_type
{
    /** Nexus Channel origin command is carried in ASCII digits, such as
     * a command embedded in a passthrough keycode.
     */
    NX_CHANNEL_ORIGIN_COMMAND_BEARER_TYPE_ASCII_DIGITS = 0,
};

/*! \brief Handle a Nexus Channel Origin Command
 *
 *  After receiving a Nexus Channel Origin Command, pass it to Nexus Channel
 *  core for processing by calling this function.
 *
 *  Origin commands are generated by a backend ('origin'), which sends
 * command data down to the device.
 *
 * \param bearer_type How is this origin command encoded?
 * \param command_data Pointer to buffer containing the origin command
 * \param command_length number of bytes to read from `command_data`
 * \return True if command was parsed successfully, false otherwise
 */
nx_channel_error nx_channel_handle_origin_command(
    const enum nx_channel_origin_command_bearer_type bearer_type,
    const void* command_data,
    const uint32_t command_length);

/*! \brief Handle incoming Nexus Channel application packet
 *
 * After receiving data from another Nexus Channel device, call this
 * function to send the data to Nexus Channel for processing.
 *
 * This function expects that any link layer or physical layer metadata
 * has been removed, and the application payload is contained within
 * `bytes_received`.
 *
 * See also: `nxp_channel_network_send`, for outbound data from Nexus
 * Channel.
 *
 * \param bytes_received pointer to received application data
 * \param bytes_count number of bytes to read from `bytes_received`
 * \param source Nexus ID of sending device
 * \return `nx_channel_error` indicating success or failure (and cause)
 */
nx_channel_error nx_channel_network_receive(const void* const bytes_received,
                                            uint32_t bytes_count,
                                            const struct nx_id* const source);

/*! \brief Return the number of current Channel Links
 *
 * Returns 0 if no links are established, returns > 1 representing the
 * number of Nexus Channel links that are established to/from this device in
 * all roles (relevant for 'dual mode' devices which can act as accessory
 * and controller role simultaneously).
 *
 * \return number of Nexus Channel links currently established
 */
uint8_t nx_channel_link_count(void);

#ifdef __cplusplus
}
#endif

#endif /* end of include guard: NEXUS__INC__NX_CHANNEL_H_ */
