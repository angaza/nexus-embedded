/** \file nexus_channel_security.h
 * Nexus Channel Security Header for OC/IoTivity consumption
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * Functions that provide 'application layer' security (from Nexus) that
 * must be exposed to the OC/IoTivity code.
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#ifndef NEXUS__SRC__NEXUS_CHANNEL_SECURITY_H_
#define NEXUS__SRC__NEXUS_CHANNEL_SECURITY_H_

#include "src/nexus_channel_core.h"

#if NEXUS_CHANNEL_ENABLED

#include <stdbool.h>
#include <stdint.h>

#include "oc/include/oc_ri.h"

/** Authenticate message against Nexus Channel security.
  *
  * If the message contains Nexus security information, then that security
  * information will be checked against currently active Nexus links. If
  * the message is unsecured, then it will only pass authentication if it
  * is bound for an unsecured resource method. The method will return an
  * appropriate CoAP message status.
  *
  * \param endpoint pointer to `oc_endpoint_t` representing the endpoint of the
  * received CoAP message. Neither pointer nor value shall be modified
  * \param pkt pointer to the `coap_packet_t` that contains the received CoAP
  * message information. The pointer shall not be modified but the packet
  * itself may be changed for a secured and authenticated message
  * \return CoAP status code representing Nexus Channel authentication check
  * result
  */
coap_status_t
nexus_channel_authenticate_message(const oc_endpoint_t* const endpoint,
                                coap_packet_t* const pkt);

/** Send Nexus Channel nonce sync message. Used in secure Nexus Channel
  * messaging.
  *
  * \param existing_pkt pointer to `coap_packet_t` that contains the messaging
  * parameters needed to construct a nonce sync message
  * \param desired_nonce desired nonce that the receiver of the message
  * should sync to
  */
void nexus_channel_send_nonce_reset_message(coap_packet_t* existing_pkt, uint32_t desired_nonce);

#endif /* NEXUS_CHANNEL_ENABLED */
#endif /* ifndef NEXUS__SRC__NEXUS_CHANNEL_SECURITY_H_ */
