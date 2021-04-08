/*
// Copyright (c) 2016 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/
/*
 *
 * Copyright (c) 2013, Institute for Pervasive Computing, ETH Zurich
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 * Modifications (c) 2020 Angaza, Inc.
 */

#pragma GCC diagnostic push
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#pragma GCC diagnostic ignored "-Wcomment"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"

#include <stdio.h>
#include <string.h>

#include "coap.h"
#include "transactions.h"
/*
#ifdef OC_TCP
#include "coap_signal.h"
#endif // OC_TCP

#ifdef OC_SECURITY
#include "security/oc_tls.h"
#endif
*/
/*---------------------------------------------------------------------------*/
/*- Variables ---------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
static uint16_t current_mid = 0;

coap_status_t coap_status_code = COAP_NO_ERROR;
/*---------------------------------------------------------------------------*/
static uint32_t coap_parse_int_option(uint8_t* bytes, size_t length)
{
    uint32_t var = 0;
    size_t i = 0;

    while (i < length)
    {
        var <<= 8;
        var |= bytes[i++];
    }
    return var;
}
/*---------------------------------------------------------------------------*/
static uint8_t coap_option_nibble(size_t value)
{
    if (value < 13)
    {
        return (uint8_t) value;
    }
    else if (value <= 0xFF + 13)
    {
        return 13;
    }
    else
    {
        return 14;
    }
}
/*---------------------------------------------------------------------------*/
static size_t
coap_set_option_header(unsigned int delta, size_t length, uint8_t* buffer)
{
    size_t written = 0;

    if (buffer)
    {
        buffer[0] = coap_option_nibble(delta) << 4 | coap_option_nibble(length);
    }

    if (delta > 268)
    {
        ++written;
        if (buffer)
        {
            buffer[written] = ((delta - 269) >> 8) & 0xff;
        }
        ++written;
        if (buffer)
        {
            buffer[written] = (delta - 269) & 0xff;
        }
    }
    else if (delta > 12)
    {
        ++written;
        if (buffer)
        {
            buffer[written] = (uint8_t)(delta - 13);
        }
    }

    if (length > 268)
    {
        ++written;
        if (buffer)
        {
            buffer[written] = ((length - 269) >> 8) & 0xff;
        }
        ++written;
        if (buffer)
        {
            buffer[written] = (length - 269) & 0xff;
        }
    }
    else if (length > 12)
    {
        ++written;
        if (buffer)
        {
            buffer[written] = (uint8_t)(length - 13);
        }
    }

    if (buffer)
    {
        // OC_DBG("WRITTEN %zu B opt header", 1 + written);
    }

    return ++written;
}
/*---------------------------------------------------------------------------*/
static size_t coap_serialize_int_option(unsigned int number,
                                        unsigned int current_number,
                                        uint8_t* buffer,
                                        uint32_t value)
{
    size_t i = 0;

    if (0xFF000000 & value)
    {
        ++i;
    }
    if (0xFFFF0000 & value)
    {
        ++i;
    }
    if (0xFFFFFF00 & value)
    {
        ++i;
    }
    if (0xFFFFFFFF & value)
    {
        ++i;
    }
    if (buffer)
    {
        // OC_DBG("OPTION %u (delta %u, len %zu)", number, number -
        // current_number, i);
    }

    i = coap_set_option_header(number - current_number, i, buffer);

    if (0xFF000000 & value)
    {
        if (buffer)
        {
            buffer[i] = (uint8_t)(value >> 24);
        }
        i++;
    }
    if (0xFFFF0000 & value)
    {
        if (buffer)
        {
            buffer[i] = (uint8_t)(value >> 16);
        }
        i++;
    }
    if (0xFFFFFF00 & value)
    {
        if (buffer)
        {
            buffer[i] = (uint8_t)(value >> 8);
        }
        i++;
    }
    if (0xFFFFFFFF & value)
    {
        if (buffer)
        {
            buffer[i] = (uint8_t)(value);
        }
        i++;
    }
    return i;
}
/*---------------------------------------------------------------------------*/
static size_t coap_serialize_array_option(unsigned int number,
                                          unsigned int current_number,
                                          uint8_t* buffer,
                                          uint8_t* array,
                                          size_t length,
                                          char split_char)
{
    size_t i = 0;

    if (buffer)
    {
        // OC_DBG("ARRAY type %u, len %zu, full [%.*s]", number, length,
        // (int)length,
        //       array);
    }

    if (split_char != '\0')
    {
        size_t j;
        uint8_t* part_start = array;
        uint8_t* part_end = NULL;
        size_t temp_length;

        for (j = 0; j <= length + 1; ++j)
        {
            if (buffer)
            {
                // OC_DBG("STEP %zu/%zu (%c)", j, length, array[j]);
            }

            if (array[j] == split_char || j == length)
            {
                part_end = array + j;
                temp_length = part_end - part_start;

                if (buffer)
                {
                    i += coap_set_option_header(
                        number - current_number, temp_length, &buffer[i]);
                    memcpy(&buffer[i], part_start, temp_length);
                }
                else
                {
                    i += coap_set_option_header(
                        number - current_number, temp_length, NULL);
                }

                i += temp_length;

                if (buffer)
                {
                    // OC_DBG("OPTION type %u, delta %u, len %zu, part [%.*s]",
                    // number,
                    //       number - current_number, i, (int)temp_length,
                    //       part_start);
                }

                ++j; /* skip the splitter */
                current_number = number;
                part_start = array + j;
            }
        } /* for */
    }
    else
    {

        if (buffer)
        {
            i += coap_set_option_header(
                number - current_number, length, &buffer[i]);
            memcpy(&buffer[i], array, length);
        }
        else
        {
            i += coap_set_option_header(number - current_number, length, NULL);
        }

        i += length;

        if (buffer)
        {
            // OC_DBG("OPTION type %u, delta %u, len %zu", number,
            //       number - current_number, length);
        }
    }

    return i;
}
/*---------------------------------------------------------------------------*/
static void coap_merge_multi_option(char** dst,
                                    size_t* dst_len,
                                    uint8_t* option,
                                    size_t option_len,
                                    char separator)
{
    // merge multiple options
    if (*dst_len > 0)
    {
        // dst already contains an option: concatenate
        (*dst)[*dst_len] = separator;
        *dst_len += 1;

        // memmove handles 2-byte option headers
        memmove((*dst) + (*dst_len), option, option_len);

        *dst_len += option_len;
    }
    else
    {
        // dst is empty: set to option
        *dst = (char*) option;
        *dst_len = option_len;
    }
}
/*---------------------------------------------------------------------------*/
/* It just calculates size of option when option_array is NULL */
static size_t coap_serialize_options(void* packet, uint8_t* option_array)
{
    coap_packet_t* const coap_pkt = (coap_packet_t*) packet;
    uint8_t* option = option_array;
    unsigned int current_number = 0;
    size_t option_length = 0;

// avoid static analysis errors when OC_DBG is a no-op (identical branches).
#ifdef CONFIG_NEXUS_COMMON_OC_DEBUG_LOG_ENABLED
    if (option)
    {
        OC_DBG("Serializing options at %p", option);
    }
    else
    {
        OC_DBG("Calculating size of options");
    }
#endif
    // COAP_SERIALIZE_INT_OPTION(COAP_OPTION_OBSERVE, observe, "Observe");
    COAP_SERIALIZE_STRING_OPTION(
        COAP_OPTION_URI_PATH, uri_path, '/', "Uri-Path");
    COAP_SERIALIZE_INT_OPTION(
        COAP_OPTION_CONTENT_FORMAT, content_format, "Content-Format");
    COAP_SERIALIZE_STRING_OPTION(
        COAP_OPTION_URI_QUERY, uri_query, '&', "Uri-Query");

    return option_length;
}
/*---------------------------------------------------------------------------*/
static coap_status_t coap_parse_token_option(void* packet,
                                             uint8_t* data,
                                             uint32_t data_len,
                                             uint8_t* current_option)
{
    coap_packet_t* const coap_pkt = (coap_packet_t*) packet;

    memcpy(coap_pkt->token, current_option, coap_pkt->token_len);
    // OC_DBG("Token (len %u)", coap_pkt->token_len);
    // OC_LOGbytes(coap_pkt->token, coap_pkt->token_len);

    /* parse options */
    memset(coap_pkt->options, 0, sizeof(coap_pkt->options));
    current_option += coap_pkt->token_len;

    unsigned int option_number = 0;
    unsigned int option_delta = 0;
    size_t option_length = 0;

    while (current_option < data + data_len)
    {
        /* payload marker 0xFF, currently only checking for 0xF* because rest is
         * reserved */
        if ((current_option[0] & 0xF0) == 0xF0)
        {
            coap_pkt->payload = ++current_option;
            coap_pkt->payload_len =
                data_len - (uint32_t)(coap_pkt->payload - data);

            if (coap_pkt->transport_type == COAP_TRANSPORT_UDP &&
                coap_pkt->payload_len > (uint32_t) OC_BLOCK_SIZE)
            {
                coap_pkt->payload_len = (uint32_t) OC_BLOCK_SIZE;
                /* null-terminate payload */
            }
            coap_pkt->payload[coap_pkt->payload_len] = '\0';

            break;
        }

        option_delta = current_option[0] >> 4;
        option_length = current_option[0] & 0x0F;
        ++current_option;

        if (option_delta == 13)
        {
            option_delta += current_option[0];
            ++current_option;
        }
        else if (option_delta == 14)
        {
            option_delta += 255;
            option_delta += current_option[0] << 8;
            ++current_option;
            option_delta += current_option[0];
            ++current_option;
        }

        if (option_length == 13)
        {
            option_length += current_option[0];
            ++current_option;
        }
        else if (option_length == 14)
        {
            option_length += 255;
            option_length += current_option[0] << 8;
            ++current_option;
            option_length += current_option[0];
            ++current_option;
        }

        option_number += option_delta;

        // if (option_number <= COAP_OPTION_SIZE1)
        {
            OC_DBG("OPTION %u (delta %u, len %zu):",
                   option_number,
                   option_delta,
                   option_length);
            SET_OPTION(coap_pkt, option_number);
        }
        if (current_option + option_length > data + data_len)
        {
            OC_WRN("Unsupported option");
            return BAD_OPTION_4_02;
        }

        switch (option_number)
        {
            case COAP_OPTION_CONTENT_FORMAT:
                coap_pkt->content_format = (uint16_t) coap_parse_int_option(
                    current_option, option_length);
                OC_DBG("  Content-Format [%u]", coap_pkt->content_format);
                if (coap_pkt->content_format != APPLICATION_VND_OCF_CBOR &&
                    coap_pkt->content_format != APPLICATION_COSE_MAC0)
                {
                    return UNSUPPORTED_MEDIA_TYPE_4_15;
                }
                break;
            case COAP_OPTION_URI_PATH:
                /* coap_merge_multi_option() operates in-place on the IPBUF, but
                 * final packet field should be const string -> cast to string
                 */
                coap_merge_multi_option((char**) &(coap_pkt->uri_path),
                                        &(coap_pkt->uri_path_len),
                                        current_option,
                                        option_length,
                                        '/');
                OC_DBG("  Uri-Path [%.*s]",
                       (int) coap_pkt->uri_path_len,
                       coap_pkt->uri_path);
                break;
            case COAP_OPTION_URI_QUERY:
                /* coap_merge_multi_option() operates in-place on the IPBUF, but
                 * final packet field should be const string -> cast to string
                 */
                coap_merge_multi_option((char**) &(coap_pkt->uri_query),
                                        &(coap_pkt->uri_query_len),
                                        current_option,
                                        option_length,
                                        '&');
                OC_DBG("  Uri-Query [%.*s]",
                       (int) coap_pkt->uri_query_len,
                       coap_pkt->uri_query);
                break;
                /*
                case COAP_OPTION_OBSERVE:
                    coap_pkt->observe =
                        coap_parse_int_option(current_option, option_length);
                    // OC_DBG("  Observe [%lu]", (unsigned
                long)coap_pkt->observe); break;
                break;
                */
            default:
                // OC_DBG("  unknown (%u)", option_number);
                // check if critical (odd)
                if (option_number & 1)
                {
                    OC_WRN("Unsupported critical option");
                    return BAD_OPTION_4_02;
                }
        }
        current_option += option_length;
    } /* for */
    OC_DBG("-Done parsing-------");

    return COAP_NO_ERROR;
}
/*---------------------------------------------------------------------------*/
/*- Internal API ------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
void coap_init_connection(void)
{
    // initialize transaction ID
    current_mid = (uint16_t) oc_random_value();
}
/*---------------------------------------------------------------------------*/
uint16_t coap_get_mid(void)
{
    return ++current_mid;
}
/*---------------------------------------------------------------------------*/
void coap_udp_init_message(void* packet,
                           coap_message_type_t type,
                           uint8_t code,
                           uint16_t mid)
{
    coap_packet_t* const coap_pkt = (coap_packet_t*) packet;

    /* Important thing */
    memset(coap_pkt, 0, sizeof(coap_packet_t));

    coap_pkt->transport_type = COAP_TRANSPORT_UDP;
    coap_pkt->type = type;
    coap_pkt->code = code;
    coap_pkt->mid = mid;
}
/*---------------------------------------------------------------------------*/
static void coap_udp_set_header_fields(void* packet)
{
    coap_packet_t* const coap_pkt = (coap_packet_t*) packet;

    coap_pkt->buffer[0] = 0x00;
    coap_pkt->buffer[0] |= COAP_HEADER_VERSION_MASK &
                           (coap_pkt->version) << COAP_HEADER_VERSION_POSITION;
    coap_pkt->buffer[0] |=
        COAP_HEADER_TYPE_MASK & (coap_pkt->type) << COAP_HEADER_TYPE_POSITION;
    coap_pkt->buffer[0] |=
        COAP_HEADER_TOKEN_LEN_MASK & (coap_pkt->token_len)
                                         << COAP_HEADER_TOKEN_LEN_POSITION;
    coap_pkt->buffer[1] = coap_pkt->code;
    coap_pkt->buffer[2] = (uint8_t)((coap_pkt->mid) >> 8);
    coap_pkt->buffer[3] = (uint8_t)(coap_pkt->mid);
}
/*---------------------------------------------------------------------------*/
size_t coap_serialize_message(void* packet, uint8_t* buffer)
{
    if (!packet || !buffer)
    {
        OC_ERR("packet: %p or buffer: %p is NULL", packet, buffer);
        return 0;
    }

    coap_packet_t* const coap_pkt = (coap_packet_t*) packet;
    uint8_t* option;
    uint8_t token_location = 0;
    size_t option_length = 0, option_length_calculation = 0,
           header_length_calculation = 0;

    /* Initialize */
    coap_pkt->buffer = buffer;
    coap_pkt->version = 1;

    /* coap header option serialize first to know total length about options */
    option_length_calculation = coap_serialize_options(coap_pkt, NULL);
    header_length_calculation += option_length_calculation;

    /* according to spec COAP_PAYLOAD_MARKER_LEN should be included
       if payload  exists */
    if (coap_pkt->payload_len > 0)
    {
        header_length_calculation += COAP_PAYLOAD_MARKER_LEN;
    }
    header_length_calculation += coap_pkt->token_len;

    /* set header fields */
    token_location = COAP_HEADER_LEN;
    header_length_calculation += token_location;

    if (header_length_calculation > COAP_MAX_HEADER_SIZE)
    {
        OC_ERR("Serialized header length %u exceeds COAP_MAX_HEADER_SIZE "
               "%u-UDP",
               (unsigned int) (header_length_calculation),
               COAP_MAX_HEADER_SIZE);
        goto exit;
    }

    OC_DBG("-Serializing MID %u to %p", coap_pkt->mid, coap_pkt->buffer);
    coap_udp_set_header_fields(coap_pkt);

    /* empty packet, dont need to do more stuff */
    if (!coap_pkt->code)
    {
        OC_DBG("Done serializing empty message at %p-", coap_pkt->buffer);
        return token_location;
    }
    /* set Token */
    // Token always 1 byte in length
    OC_DBG("Token (len %u, value %u)", coap_pkt->token_len, coap_pkt->token[0]);
    option = coap_pkt->buffer + token_location;
    // COAP_TOKEN_LEN is always 1
    *option = coap_pkt->token[0];
    ++option;

    option_length = coap_serialize_options(packet, option);
    option += option_length;

    /* Pack payload */
    if ((option - coap_pkt->buffer) <= COAP_MAX_HEADER_SIZE)
    {
        /* Payload marker */
        if (coap_pkt->payload_len > 0)
        {
            *option = 0xFF;
            ++option;
            // Angaza - prevent calling memmove with empty payload
            memmove(option, coap_pkt->payload, coap_pkt->payload_len);
        }
    }
    else
    {
        /* an error occurred: caller must check for !=0 */
        OC_WRN("Serialized header length %u exceeds COAP_MAX_HEADER_SIZE %u",
               (unsigned int) (option - coap_pkt->buffer),
               COAP_MAX_HEADER_SIZE);
        goto exit;
    }

    OC_DBG("-Done %u B (header len %u, payload len %u)-",
           (unsigned int) (coap_pkt->payload_len + option - buffer),
           (unsigned int) (option - buffer),
           (unsigned int) coap_pkt->payload_len);

    OC_DBG("Serialized bytes");
    OC_LOGbytes(coap_pkt->buffer, (coap_pkt->payload_len + option - buffer));

    return (option - buffer) + coap_pkt->payload_len; /* packet length */

exit:
    coap_pkt->buffer = NULL;
    return 0;
}
/*---------------------------------------------------------------------------*/
void coap_send_message(oc_message_t* message)
{
    OC_DBG("-sending OCF message (%u)-", (unsigned int) message->length);

    oc_send_message(message);
}
/*---------------------------------------------------------------------------*/
coap_status_t
coap_udp_parse_message(void* packet, uint8_t* data, uint16_t data_len)
{
    coap_packet_t* const coap_pkt = (coap_packet_t*) packet;
    /* initialize packet */
    memset(coap_pkt, 0, sizeof(coap_packet_t));
    /* pointer to packet bytes */
    coap_pkt->buffer = data;
    coap_pkt->transport_type = COAP_TRANSPORT_UDP;
    /* parse header fields */
    coap_pkt->version = (COAP_HEADER_VERSION_MASK & coap_pkt->buffer[0]) >>
                        COAP_HEADER_VERSION_POSITION;
    coap_pkt->type =
        (coap_message_type_t)((COAP_HEADER_TYPE_MASK & coap_pkt->buffer[0]) >>
                              COAP_HEADER_TYPE_POSITION);
    coap_pkt->token_len = (COAP_HEADER_TOKEN_LEN_MASK & coap_pkt->buffer[0]) >>
                          COAP_HEADER_TOKEN_LEN_POSITION;
    coap_pkt->code = coap_pkt->buffer[1];
    coap_pkt->mid = coap_pkt->buffer[2] << 8 | coap_pkt->buffer[3];

    if (coap_pkt->version != 1)
    {
        OC_WRN("CoAP version must be 1");
        return BAD_REQUEST_4_00;
    }

    if (coap_pkt->token_len > COAP_TOKEN_LEN)
    {
        OC_WRN("Token Length must not be more than 1");
        return BAD_REQUEST_4_00;
    }

    uint8_t* current_option = data + COAP_HEADER_LEN;

    coap_status_t ret =
        coap_parse_token_option(packet, data, data_len, current_option);
    if (COAP_NO_ERROR != ret)
    {
        OC_DBG("coap_parse_token_option failed!");
        return ret;
    }

    return COAP_NO_ERROR;
}
/*---------------------------------------------------------------------------*/
int coap_set_status_code(void* packet, unsigned int code)
{
    if (code <= 0xFF)
    {
        ((coap_packet_t*) packet)->code = (uint8_t) code;
        return 1;
    }
    else
    {
        return 0;
    }
}
/*---------------------------------------------------------------------------*/
int coap_set_token(void* packet, const uint8_t* token, size_t token_len)
{
    coap_packet_t* const coap_pkt = (coap_packet_t*) packet;
    // Should always be 1 in Nexus Channel Core
    coap_pkt->token_len = (uint8_t) MIN(COAP_TOKEN_LEN, token_len);
    memcpy(coap_pkt->token, token, coap_pkt->token_len);

    return coap_pkt->token_len;
}

int coap_get_header_content_format(void* packet, unsigned int* format)
{
    coap_packet_t* const coap_pkt = (coap_packet_t*) packet;

    if (!IS_OPTION(coap_pkt, COAP_OPTION_CONTENT_FORMAT))
    {
        return 0;
    }
    *format = coap_pkt->content_format;
    return 1;
}

int coap_set_header_content_format(void* packet, unsigned int format)
{
    coap_packet_t* const coap_pkt = (coap_packet_t*) packet;

    coap_pkt->content_format = (uint16_t) format;
    SET_OPTION(coap_pkt, COAP_OPTION_CONTENT_FORMAT);
    return 1;
}
/*---------------------------------------------------------------------------*/
size_t coap_get_header_uri_path(void* packet, const char** path)
{
    coap_packet_t* const coap_pkt = (coap_packet_t*) packet;

    if (!IS_OPTION(coap_pkt, COAP_OPTION_URI_PATH))
    {
        return 0;
    }
    *path = coap_pkt->uri_path;
    return coap_pkt->uri_path_len;
}
size_t coap_set_header_uri_path(void* packet, const char* path, size_t path_len)
{
    coap_packet_t* const coap_pkt = (coap_packet_t*) packet;

    while (path[0] == '/')
    {
        ++path;
        --path_len;
    }

    coap_pkt->uri_path = path;
    coap_pkt->uri_path_len = path_len;

    SET_OPTION(coap_pkt, COAP_OPTION_URI_PATH);
    return coap_pkt->uri_path_len;
}
/*---------------------------------------------------------------------------*/
size_t coap_get_header_uri_query(void* packet, const char** query)
{
    coap_packet_t* const coap_pkt = (coap_packet_t*) packet;

    if (!IS_OPTION(coap_pkt, COAP_OPTION_URI_QUERY))
    {
        return 0;
    }
    *query = coap_pkt->uri_query;
    return coap_pkt->uri_query_len;
}

//#ifdef OC_CLIENT (also used in Nexus tests)
size_t coap_set_header_uri_query(void* packet, const char* query)
{
    coap_packet_t* const coap_pkt = (coap_packet_t*) packet;

    while (query[0] == '?')
        ++query;

    coap_pkt->uri_query = query;
    coap_pkt->uri_query_len = strlen(query);

    SET_OPTION(coap_pkt, COAP_OPTION_URI_QUERY);
    return coap_pkt->uri_query_len;
}
//#endif

/*---------------------------------------------------------------------------*/
/*
int coap_get_header_observe(void* packet, uint32_t* observe)
{
    coap_packet_t* const coap_pkt = (coap_packet_t*) packet;

    if (!IS_OPTION(coap_pkt, COAP_OPTION_OBSERVE))
    {
        return 0;
    }
    *observe = coap_pkt->observe;
    return 1;
}
int coap_set_header_observe(void* packet, uint32_t observe)
{
    coap_packet_t* const coap_pkt = (coap_packet_t*) packet;

    coap_pkt->observe = observe;
    SET_OPTION(coap_pkt, COAP_OPTION_OBSERVE);
    return 1;
}
*/
/*---------------------------------------------------------------------------*/
int coap_get_payload(void* packet, const uint8_t** payload)
{
    coap_packet_t* const coap_pkt = (coap_packet_t*) packet;

    if (coap_pkt->payload)
    {
        *payload = coap_pkt->payload;
        return coap_pkt->payload_len;
    }
    else
    {
        *payload = NULL;
        return 0;
    }
}
int coap_set_payload(void* packet, const void* payload, size_t length)
{
    coap_packet_t* const coap_pkt = (coap_packet_t*) packet;

    coap_pkt->payload = (uint8_t*) payload;
#ifdef OC_TCP
    if (coap_pkt->transport_type == COAP_TRANSPORT_TCP)
    {
        coap_pkt->payload_len = (uint32_t) length;
    }
    else
#endif /* OC_TCP */
    {
        coap_pkt->payload_len =
            (uint16_t) MIN((unsigned) OC_BLOCK_SIZE, length);
    }

    return coap_pkt->payload_len;
}

/*---------------------------------------------------------------------------*/
#pragma GCC diagnostic pop
