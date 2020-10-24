/** \file
 * Nexus Common Internal Module (Header)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#ifndef NEXUS__SRC__COMMON__NEXUS_COMMON_INTERNAL_H_
#define NEXUS__SRC__COMMON__NEXUS_COMMON_INTERNAL_H_

#include "src/internal_channel_config.h"
#include "src/internal_keycode_config.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint32_t NEXUS_COMMON_IDLE_TIME_BETWEEN_PROCESS_CALL_SECONDS;

/** Has the Nexus system successfully initialized itself?
 *
 * Initialization is marked 'successful' once:
 *
 * 1) Implementing system calls `nx_common_init`
 * 2) Implementing system subsequently calls `nx_common_process`
 * 3) No modules raised any error during initialization
 *
 * \return True if initialized successfully, false otherwise
 */
bool nexus_common_init_completed(void);

/** Seconds since the Nexus system was started/initialized.
 *
 * \return current system uptime, in seconds
 */
uint32_t nexus_common_uptime(void);

#ifdef __cplusplus
}
#endif

#endif /* ifndef NEXUS__SRC__COMMON__NEXUS_COMMON_INTERNAL_H_ */
