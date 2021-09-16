/** \file Nexus CoAP Parser CLI Tool
 * \author Angaza
 * \copyright 2021 Angaza, Inc.
 * \license This file is released under the MIT license.
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include <stdio.h>
#include <stdlib.h>
#include "include/shared_oc_config.h"
#include "messaging/coap/coap.h"

// required to not break the Nexus library when compiling the CoAP module.
// The CoAP module currently has some state: the message ID is stored as a
// static variable. This function initializes it and so is required by any
// executable.
unsigned int oc_random_value(void)
{
    return 12345;
}

// consumes a pointer to a buffer and its length; prints
// out buffer values in zero-padded hex format
static void _print_buffer_field(const uint8_t* buf, uint8_t len)
{
    for (uint8_t i = 0; i < len; i++)
    {
        printf("%02X", buf[i]);
    }
    printf("\n");
}

/**
 * Consumes a space-delimited bytestring and parses it into CoAP fields
 * required by the Nexus CoAP spec.
 */
int main(int argc, char* argv[])
{
    if (argc == 1)
    {
        printf("Requires byestring input\n");
        return 1;
    }
    else if (argc > NEXUS_CHANNEL_MAX_COAP_TOTAL_MESSAGE_SIZE)
    {
        printf("Input of length %d exceeds maximum Nexus CoAP message size\n", argc);
        return 1;
    }
    else
    {
        // convert string inputs to int
        char* p;
        uint8_t input_bytestring[NEXUS_CHANNEL_MAX_COAP_TOTAL_MESSAGE_SIZE] = {0};
        // argc-1 to account for 1st arg (name of executable)
        uint8_t num_bytes = argc - 1;
        for (uint8_t i = 0; i < num_bytes; i++)
        {
            // expects hex format for bytestring input
            long conv = strtol(argv[i + 1], &p, 16);
            uint8_t conv_u8 = (uint8_t) conv;
            memcpy(&input_bytestring[i], &conv_u8, 1);
        }
        
        // parse
        coap_packet_t pkt[1];
        coap_status_t ret = coap_udp_parse_message(pkt, input_bytestring, num_bytes);

        // print out pkt contents
        printf("version: %d\n", pkt->version);
        printf("type: %d\n", pkt->type);
        // Nexus CoAP always has token of length 1
        printf("token_len: %d\n", pkt->token_len);
        printf("token: %d\n", pkt->token[0]);
        printf("code: %d\n", pkt->code);
        printf("message_id: %d\n", pkt->mid);
        printf("uri_path_len: %ld\n", pkt->uri_path_len);
        if (pkt->uri_path_len > 0)
        {
            printf("uri_path: ");
            _print_buffer_field(pkt->uri_path, pkt->uri_path_len);
        }
        printf("payload_len: %d\n", pkt->payload_len);
        if (pkt->payload_len > 0)
        {
            printf("payload: ");
            _print_buffer_field(pkt->payload, pkt->payload_len);
        }
        printf("content_format: %d\n", pkt->content_format); 
        printf("uri_query_len: %ld\n", pkt->uri_query_len);
        if (pkt->uri_query_len > 0) 
        {
            printf("uri_query: ");
            _print_buffer_field(pkt->uri_query, pkt->uri_query_len);
        }
    }

    return 0;
}
