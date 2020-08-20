/** \file nexus_oc_wrapper.c
 * Nexus-OC Wrapper Module (Implementation)
 * \author Angza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license.
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "src/nexus_oc_wrapper.h"
#include "include/nxp_channel.h"
#include "include/nxp_core.h"
#include "oc/include/oc_api.h"
#include "oc/include/oc_buffer.h"
#include "oc/include/oc_ri.h"
#include "oc/messaging/coap/transactions.h"
#include "src/nexus_channel_sm.h"

#if NEXUS_CHANNEL_ENABLED

// common define for the multicast OCF address
// broadcast endpoint, not dynamically allocated
// 0x02 = 'link local' scope, multicast to directly connected devices
// This address is defined as "All OCF nodes" by IANA
// FF0X:0:0:0:0:0:0:158
// https://www.iana.org/assignments/ipv6-multicast-addresses/ipv6-multicast-addresses.xhtml#ipv6-scope
oc_endpoint_t NEXUS_OC_WRAPPER_MULTICAST_OC_ENDPOINT_T_ADDR = {
    0, // no 'next'
    0, // device N/A
    IPV6 | MULTICAST, // transport flags
    {{0}}, // uuid 'di' ignored
    {{5683, // port
      {
          0xff,
          0x02,
          0,
          0,
          0,
          0,
          0,
          0,
          0,
          0,
          0,
          0,
          0,
          0,
          0x01,
          0x58 // 'all OCF addresses'
      },
      2}}, // link-local scope
    {{0, {0}, 0}}, // 'addr_local' unused
    0, // 'interface_index' unused
    0, // 'priority' unused
    0 // 'version' unused
};

bool nexus_resource_set_request_handler(oc_resource_t* resource,
                                        oc_method_t method,
                                        oc_request_callback_t callback)
{
    const oc_request_handler_t* handler = NULL;
    switch (method)
    {
        case OC_GET:
            handler = &resource->get_handler;
            break;
        case OC_POST:
            handler = &resource->post_handler;
            break;
        case OC_PUT:
            handler = &resource->put_handler;
            break;
        case OC_DELETE:
            handler = &resource->delete_handler;
            break;
        default:
            break;
    }

    if (handler == NULL)
    {
        return false;
    }
    else
    {
        /* Only update the handler if it doesn't already exist. This protects
         * against accidental resource handler registration collisions from
         * new Nexus resources and future versions of existing resources.
         */
        if (handler->cb == NULL)
        {
            // OC_DBG
            oc_resource_set_request_handler(resource, method, callback, NULL);
            return true;
        }
        else
        {
            // OC_WRN
            return false;
        }
    }
}

bool nexus_add_resource(oc_resource_t* resource)
{
    if (!resource)
    {
        return false;
    }

    // don't register to a URI that's already been registered
    if (oc_ri_get_app_resource_by_uri(resource->uri.ptr,
                                      oc_string_len(resource->uri),
                                      NEXUS_CHANNEL_NEXUS_DEVICE_ID) != NULL)
    {
        return false;
    }

    return oc_ri_add_resource(resource);
}

void oc_random_init(void)
{
    nxp_core_random_init();
}

unsigned int oc_random_value(void)
{
    return nxp_core_random_value();
}

nx_channel_error
nx_channel_network_receive(const void* const bytes_received,
                           uint32_t bytes_count,
                           const struct nx_ipv6_address* const source_address)
{
    // return early on null or invalid bytes
    if (bytes_received == 0 || bytes_count == 0)
    {
        return NX_CHANNEL_ERROR_UNSPECIFIED;
    }
    // will be deallocated in calls initiated by `oc_network_event`
    // Note: Is *not* dynamic memory allocation. `oc_allocate_message`
    // pulls from an immutable set of bytes defined at compile time,
    // memory use does not increase by calling this function.
    oc_message_t* message = oc_allocate_message();

    if (message)
    {
        PRINT("nx_channel_network: Receiving %u byte message: ", bytes_count);
        PRINTbytes(((uint8_t*) bytes_received), bytes_count);
        message->length = bytes_count;
        memcpy(message->data, bytes_received, bytes_count);
        memcpy(
            message->endpoint.addr.ipv6.address, source_address->address, 16);
        message->endpoint.device = 0;
        message->endpoint.priority = 0;
        message->endpoint.interface_index = 0;
        message->endpoint.version = OIC_VER_1_1_0;
        message->endpoint.flags = IPV6;

        // Detect the multicast 'all OCF devices' address
        if (memcmp(
                &message->endpoint.addr.ipv6.address,
                NEXUS_OC_WRAPPER_MULTICAST_OC_ENDPOINT_T_ADDR.addr.ipv6.address,
                16) == 0)
        {
            message->endpoint.flags |= MULTICAST;
        }

        // pass the message into the Nexus Channel stack, where it will
        // be processed and deallocated when complete.
        oc_network_event(message);
    }

    // trigger processing so that IoTivity core can receive the message
    nxp_core_request_processing();
    return NX_CHANNEL_ERROR_NONE;
}

void nexus_oc_wrapper_oc_endpoint_to_nx_ipv6(
    const oc_endpoint_t* const source_endpoint,
    struct nx_ipv6_address* dest_nx_ipv6_address)
{
    NEXUS_ASSERT(source_endpoint->flags & IPV6,
                 "Source IP address is not IPV6");
    NEXUS_STATIC_ASSERT(
        sizeof(((struct nx_ipv6_address*) 0)->address) ==
            sizeof(((oc_ipv6_addr_t*) 0)->address),
        "Endpoint IPV6 or nx_ipv6_address is not 16 bytes in length.");

    if (source_endpoint->addr.ipv6.scope == 0)
    {
        // global scope
        dest_nx_ipv6_address->global_scope = true;
    }
    else
    {
        dest_nx_ipv6_address->global_scope = false;
    }

    memcpy(&dest_nx_ipv6_address->address, // ipv6 address (16 bytes)
           &source_endpoint->addr.ipv6.address,
           sizeof(((struct nx_ipv6_address*) 0)->address));
}

bool nexus_oc_wrapper_oc_endpoint_to_nx_id(
    const oc_endpoint_t* const source_endpoint, struct nx_id* dest_nx_id)
{
    struct nx_ipv6_address tmp_nx_addr = {0};
    nexus_oc_wrapper_oc_endpoint_to_nx_ipv6(source_endpoint, &tmp_nx_addr);
    memcpy(&tmp_nx_addr.address, // ipv6 address (16 bytes)
           &source_endpoint->addr.ipv6.address,
           sizeof(((struct nx_ipv6_address*) 0)->address));

    return nx_core_ipv6_address_to_nx_id(&tmp_nx_addr, dest_nx_id);
}

static void _nexus_oc_wrapper_inner_network_send(const oc_message_t* message,
                                                 bool is_multicast)
{
    struct nx_ipv6_address dest_address = {0};
    struct nx_ipv6_address source_address = {0};
    struct nx_id id = nxp_channel_get_nexus_id();

    nexus_oc_wrapper_oc_endpoint_to_nx_ipv6(&message->endpoint, &dest_address);

    nx_core_nx_id_to_ipv6_address(&id, &source_address);

    nxp_channel_network_send(message->data,
                             message->length,
                             &source_address,
                             &dest_address,
                             is_multicast);
}

int oc_send_buffer(oc_message_t* message)
{
    bool multicast = false;
    message->endpoint.flags |= IPV6;
    if (message->endpoint.flags & MULTICAST)
    {
        multicast = true;
    }
    _nexus_oc_wrapper_inner_network_send(message, multicast);
    return 0;
}

void oc_send_discovery_request(oc_message_t* message)
{
    oc_send_buffer(message);
}

void nexus_oc_wrapper_repack_buffer_secured(
    uint8_t* buffer, nexus_security_mode0_cose_mac0_t* cose_mac0)
{
    // first, set up a new OC rep allowing us to encode our 'new message'
    // into a payload. This buffer temporarily exists within this packing step
    uint8_t payload_buffer[OC_BLOCK_SIZE];
    oc_rep_new(payload_buffer, OC_BLOCK_SIZE);
    oc_rep_begin_root_object();
    // 'protected' in a bstr;
    oc_rep_set_byte_string(root, p, (uint8_t*) &cose_mac0->protected_header, 1);
    // 'unprotected' elements as a map of length 2
    oc_rep_open_object(root, u);
    oc_rep_set_uint(u, 4, cose_mac0->kid);
    oc_rep_set_uint(u, 5, cose_mac0->nonce);
    oc_rep_close_object(root, u);
    // 'payload' in a bstr
    oc_rep_set_byte_string(root, d, cose_mac0->payload, cose_mac0->payload_len);
    // 'tag' in a bstr
    oc_rep_set_byte_string(
        root, m, (uint8_t*) cose_mac0->mac, sizeof(struct nexus_check_value));
    oc_rep_end_root_object();

    // New payload size, after packing as a COSE MAC0 object
    // Required for downstream logic which will set the CoAP packet payload
    // length fields
    int payload_size = oc_rep_get_encoded_payload_size();

    // now, copy back the packed buffer back over the packed application data
    memcpy(buffer, payload_buffer, payload_size);
}

#endif /* NEXUS_CHANNEL_ENABLED */
