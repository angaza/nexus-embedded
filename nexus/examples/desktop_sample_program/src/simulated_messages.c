/** \file simulated_messages.c
 * \brief (Internal) Simulated messages for demonstration.
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies or
 * substantial portions of the Software.
 *
 * Warning: Nothing in this file should be required by an implementer of Nexus
 * Channel! This file exists only to simplify what files must be included
 * inside an example without requiring multiple 'full' Nexus Channel devices
 * running at the same time.
 *
 * Instead, an implementing product only needs to call
 * `nx_channel_network_receive`
 * to pass the received application data into Nexus Channel.
 */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include "network.h"
#include "oc/include/oc_api.h"
#include "oc/include/oc_buffer.h"
#include "oc/include/oc_endpoint.h"
#include "oc/messaging/coap/coap.h"
#include "src/nexus_oc_wrapper.h"
#include <stdint.h>

extern void
nexus_oc_wrapper_nx_id_to_oc_endpoint(const struct nx_id* input_id,
                                      struct oc_endpoint_t* output_ep);

// simulate an accessory receiving data (assuming the data is a valid handshake
// message for that accessory), and simulate a successful response
void simulate_message_link_handshake_response_accessory(
    void* data, uint32_t data_len, struct nx_id* source_nx_id)
{
    static coap_packet_t rcvd_coap_packet[1];

    // reconstruct it as an IoTivity message
    oc_message_t message = {0};
    message.length = data_len;
    memcpy(message.data, data, data_len);
    nexus_oc_wrapper_nx_id_to_oc_endpoint(source_nx_id, &message.endpoint);

    (void) coap_udp_parse_message(
        rcvd_coap_packet, message.data, message.length);

    PRINT("Simulated accessory: Received CoAP code %d\n",
          rcvd_coap_packet->code);
    // In demonstration, the link key generated by the example origin
    // 'link accessory' origin command `*814 263 468 067 164#` is
    // [A7 73 86 ED F5 22 C2 7E 3B F1 2C 86 32 D8 AA CA] (this is
    // deterministic based on both the keycode and the salt used by the
    // 'controller' in this demonstration program, which is why the salt
    // must be constant).
    //
    // Here, we simulate an accessory generating the same link key on its
    // side (as would happen in production), and responding with a
    // confirmation response -- consisting of
    // a MAC computed over the inverted salt (the same salt that was
    // generated by the controller and sent in the multicast 'handshake'
    // message)
    // The computed MAC is {0xd9, 0x9b, 0x1, 0xef, 0xfa, 0xa6, 0x83, 0xca}
    uint8_t resp_data_cbor[14] = {0xBF,
                                  0x62,
                                  0x72,
                                  0x44,
                                  0x48,
                                  0x73,
                                  0xBB,
                                  0xE1,
                                  0x6E,
                                  0x4E,
                                  0x7F,
                                  0xE8,
                                  0x37,
                                  0xFF};

    coap_packet_t resp_packet = {0};
    coap_udp_init_message(
        &resp_packet, COAP_TYPE_NON, CREATED_2_01, rcvd_coap_packet->mid);
    coap_set_token(
        &resp_packet, rcvd_coap_packet->token, rcvd_coap_packet->token_len);

    if (rcvd_coap_packet->uri_path == NULL ||
        rcvd_coap_packet->uri_path_len == 0)
    {
        // protect against calls with invalid rcvd packet.
        return;
    }
    coap_set_header_uri_path(&resp_packet,
                             rcvd_coap_packet->uri_path,
                             rcvd_coap_packet->uri_path_len);
    coap_set_payload(&resp_packet, resp_data_cbor, 14);

    uint8_t response_buffer[200] = {0};
    // serialize it into a buffer
    uint32_t response_length =
        coap_serialize_message(&resp_packet, &response_buffer[0]);

    struct nx_id simulated_client_nx_id = {0, 0xAFBB440D};

    receive_data_from_network(
        response_buffer, response_length, &simulated_client_nx_id);
}

#pragma GCC diagnostic pop
