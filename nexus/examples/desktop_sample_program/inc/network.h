/** \file network.h
 * \brief A mock implementation of networking interface to Nexus Channel
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#include "nxp_channel.h"
#include <stdint.h>

// Product-specific function to 'receive' incoming data
void receive_data_from_network(void* data,
                               uint32_t data_len,
                               struct nx_ipv6_address* source_addr);

// Exposed for demonstration in this example program, used to 'simulate'
// a reply from an accessory.
void enable_simulated_accessory_response(void);
void disable_simulated_accessory_response(void);
