/** \file network.h
 * \brief A demo implementation of networking interface to Nexus Channel
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "nx_common.h"
#include <stdint.h>

// Product-specific function to 'receive' incoming data
void receive_data_from_network(void* data,
                               uint32_t data_len,
                               struct nx_id* source_nx_id);

#ifdef __cplusplus
}
#endif