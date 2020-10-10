/** \file nexus_oc_wrapper.c
 * Nexus-OC Wrapper Module (Implementation)
 * \author Angaza
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

#if NEXUS_CHANNEL_CORE_ENABLED

// common define for the multicast OCF address
// broadcast endpoint, not dynamically allocated
// 0x02 = 'link local' scope, multicast to directly connected devices
// This address is defined as "All OCF nodes" by IANA
// FF0X:0:0:0:0:0:0:158
// https://www.iana.org/assignments/ipv6-multicast-addresses/ipv6-multicast-addresses.xhtml#ipv6-scope
oc_endpoint_t NEXUS_OC_WRAPPER_MULTICAST_OC_ENDPOINT_T_ADDR = {
    0, // no 'next'
    0, // device N/A
    (enum transport_flags)(IPV6 | MULTICAST), // transport flags
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
    (ocf_version_t) 0 // 'version' unused
};

struct nx_id NEXUS_OC_WRAPPER_MULTICAST_NX_ID = {
    0xFF00, // authority ID
    0x158, // device ID
};

bool nexus_add_resource(oc_resource_t* resource)
{
    if (!resource)
    {
        return false;
    }

    // don't register to a URI that's already been registered
    if (oc_ri_get_app_resource_by_uri((const char*) resource->uri.ptr,
                                      oc_string_len(resource->uri),
                                      NEXUS_CHANNEL_NEXUS_DEVICE_ID) != NULL)
    {
        return false;
    }

    return oc_ri_add_resource(resource);
}

nx_channel_error
nexus_channel_set_request_handler(oc_resource_t* resource,
                                  oc_method_t method,
                                  oc_request_callback_t callback,
                                  bool secured)
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
        default:
            // PUT and DELETE intentionally not handled
            return NX_CHANNEL_ERROR_METHOD_UNSUPPORTED;
    }

    NEXUS_ASSERT(handler != NULL, "Handler impossibly NULL.");
    if (handler->cb != NULL)
    {
        return NX_CHANNEL_ERROR_ACTION_REJECTED;
    }

    /* Only update the handler if it doesn't already exist. This protects
     * against accidental resource handler registration collisions from
     * new Nexus resources and future versions of existing resources.
     */
    oc_resource_set_request_handler(resource, method, callback, NULL);

    #if NEXUS_CHANNEL_LINK_SECURITY_ENABLED
    // if secured, attempt to store the resource method security
    // configuration
    if (secured &&
        nexus_channel_sm_nexus_resource_method_new(resource, method) == NULL)
    {
        // unset the resource request handler
        OC_WRN("could not set the resource method security");
        oc_resource_set_request_handler(resource, method, NULL, NULL);
        return NX_CHANNEL_ERROR_UNSPECIFIED;
    }
    #else
    NEXUS_ASSERT(
        !secured,
        "Security options not compiled in, cannot secure resource method");
    #endif // NEXUS_CHANNEL_LINK_SECURITY_ENABLED
    return NX_CHANNEL_ERROR_NONE;
}

void oc_random_init(void)
{
    nxp_core_random_init();
}

unsigned int oc_random_value(void)
{
    return nxp_core_random_value();
}

nx_channel_error nx_channel_network_receive(const void* const bytes_received,
                                            uint32_t bytes_count,
                                            const struct nx_id* const source)
{
    // return early on null or invalid bytes
    if (bytes_received == 0 || bytes_count == 0)
    {
        return NX_CHANNEL_ERROR_UNSPECIFIED;
    }
    else if (bytes_count > NEXUS_CHANNEL_APPLICATION_LAYER_MAX_MESSAGE_BYTES)
    {
        return NX_CHANNEL_ERROR_MESSAGE_TOO_LARGE;
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

        // Convert into oc_endpoint form expected by IoTivity
        oc_endpoint_t source_endpoint;
        memset(&source_endpoint, 0x00, sizeof(oc_endpoint_t));
        nexus_oc_wrapper_nx_id_to_oc_endpoint(source, &source_endpoint);
        oc_endpoint_copy(&message->endpoint, &source_endpoint);

        // Message will be processed and deallocated during main event loop
        oc_network_event(message);
    }

    // trigger processing so that IoTivity core can receive the message
    nxp_core_request_processing();
    return NX_CHANNEL_ERROR_NONE;
}

void nexus_oc_wrapper_oc_endpoint_to_nx_id(const oc_endpoint_t* const input_ep,
                                           struct nx_id* output_id)
{
    NEXUS_ASSERT(input_ep->flags & IPV6,
                 "Input endpoint IP address is not IPV6");
    NEXUS_STATIC_ASSERT(sizeof(((oc_ipv6_addr_t*) 0)->address) == 16,
                        "Endpoint IPV6 is not 16 bytes in length.");

    // zero dest / output array
    memset(output_id, 0x00, sizeof(struct nx_id));

    const oc_ipv6_addr_t* const ipv6_addr = &input_ep->addr.ipv6;

    // Incoming data is encoded as big endian inside the IPV6 address array.
    // Least significant byte is in address 15, MSB is in address 10
    // (big endian - biggest value is at the lowest memory address)
    output_id->device_id =
        (uint32_t)(((uint32_t)(ipv6_addr->address[10]) << 24) |
                   (uint32_t)((ipv6_addr->address[13] << 16)) |
                   (uint32_t)((ipv6_addr->address[14] << 8)) |
                   (uint32_t)((ipv6_addr->address[15])));

    // need to un-invert the 6th bit (0x02) which was inverted when creating
    // an IPV6 address from Nexus ID.
    output_id->authority_id = (uint16_t)(((ipv6_addr->address[8] ^ 0x02) << 8) |
                                         (ipv6_addr->address[9]));
}

void nexus_oc_wrapper_nx_id_to_oc_endpoint(const struct nx_id* input_id,
                                           oc_endpoint_t* output_ep)
{
    // 0, 1, 2 ... 5, 6, 7 are from Nexus ID
    // 3, 4 are fixed in EUI-64 expansion
    uint8_t ipv6_interface_addr[8] = {0, 0, 0, 0xFF, 0xFE, 0, 0, 0};

    // zero dest / output array
    memset(output_ep, 0x00, sizeof(oc_endpoint_t));

    // big endian as conventional 'network order'
    const uint16_t authority_id_big_endian =
        nexus_endian_htobe16(input_id->authority_id);
    const uint32_t device_id_big_endian =
        nexus_endian_htobe32(input_id->device_id);

    NEXUS_STATIC_ASSERT(2 == sizeof(uint16_t), "Unexpected uint16_t size");
    NEXUS_STATIC_ASSERT(4 == sizeof(uint32_t), "Unexpected uint32_t size");

    memcpy(&ipv6_interface_addr[0], &authority_id_big_endian, 2);

    // invert bit at index 6 (bit 7) from the left in the first octet
    // 0x02 = 0b00000010
    ipv6_interface_addr[0] ^= 0x02;

    // Byte order is in big endian format now (in memory), so copy in order.
    ipv6_interface_addr[2] =
        (uint8_t)(device_id_big_endian & 0xff); // first byte
    // bytes 3 and 4 are 0xFFFE (EUI-64)
    ipv6_interface_addr[5] =
        (uint8_t)((device_id_big_endian >> 8) & 0xff); // second
    ipv6_interface_addr[6] =
        (uint8_t)((device_id_big_endian >> 16) & 0xff); // third
    ipv6_interface_addr[7] =
        (uint8_t)((device_id_big_endian >> 24) & 0xff); // fourth

    // link-local prefix (Future: Support global Nexus IDs from ARIN block)
    output_ep->addr.ipv6.address[0] = 0xFE;
    output_ep->addr.ipv6.address[1] = 0x80;

    // byte indices 2-7 remain 0, 8-15 are the Nexus ID as EUI-64 (interface
    // ID)
    NEXUS_STATIC_ASSERT(
        sizeof(((oc_ipv6_addr_t*) 0)->address) == 16,
        "Cannot copy into output_ep, unexpected ipv6 address size.");

    // Copy last 8 bytes of IPV6 address
    memcpy(&output_ep->addr.ipv6.address[8], ipv6_interface_addr, 8);

    // Set flags and OC-specific parameters
    output_ep->flags = (enum transport_flags)(output_ep->flags | IPV6);
    output_ep->version = OIC_VER_1_1_0;

    if (memcmp(input_id,
               &NEXUS_OC_WRAPPER_MULTICAST_NX_ID,
               sizeof(struct nx_id)) == 0)
    {
        output_ep->flags = (enum transport_flags)(output_ep->flags | MULTICAST);
    }
    output_ep->addr.ipv6.scope = 2;
    output_ep->addr.ipv6.port = 5683;
}

// Returns 0 if sent to link layer successfully, nonzero otherwise
static int _nexus_oc_wrapper_inner_network_send(const oc_message_t* message,
                                                bool is_multicast)
{
    const struct nx_id source_nx_id = nxp_channel_get_nexus_id();
    struct nx_id dest_nx_id_tmp = {0, 0};
    struct nx_id* dest_id;

    if (is_multicast)
    {
        dest_id = &NEXUS_OC_WRAPPER_MULTICAST_NX_ID;
    }
    else
    {
        nexus_oc_wrapper_oc_endpoint_to_nx_id(&message->endpoint,
                                              &dest_nx_id_tmp);
        dest_id = &dest_nx_id_tmp;
    }

    NEXUS_ASSERT_FAIL_IN_DEBUG_ONLY(
        message->length <= NEXUS_CHANNEL_APPLICATION_LAYER_MAX_MESSAGE_BYTES,
        "Message exceeds max expected limit!");
    if (message->length > NEXUS_CHANNEL_APPLICATION_LAYER_MAX_MESSAGE_BYTES)
    {
        OC_WRN("Cannot send message of length %u", message->length);
        return 1;
    }

    PRINT("nx_channel_network: Sending %lu byte message: ", message->length);
    PRINTbytes(((uint8_t*) message->data), message->length);

    nxp_channel_network_send(message->data,
                             (uint32_t) message->length,
                             &source_nx_id,
                             dest_id,
                             is_multicast);
    return 0;
}

int oc_send_buffer(oc_message_t* message)
{
    bool multicast = false;
    message->endpoint.flags =
        (enum transport_flags)(message->endpoint.flags | IPV6);
    if (message->endpoint.flags & MULTICAST)
    {
        multicast = true;
    }
    // nexus_oc_wrapper_inner_network_send returns
    return _nexus_oc_wrapper_inner_network_send(message, multicast);
}

void oc_send_discovery_request(oc_message_t* message)
{
    oc_send_buffer(message);
}

    #if NEXUS_CHANNEL_LINK_SECURITY_ENABLED
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
    size_t payload_size = (size_t) oc_rep_get_encoded_payload_size();

    // now, copy back the packed buffer back over the packed application data
    memcpy(buffer, payload_buffer, payload_size);
}
    #endif /* NEXUS_CHANNEL_LINK_SECURITY_ENABLED */

#endif /* NEXUS_CHANNEL_CORE_ENABLED */
