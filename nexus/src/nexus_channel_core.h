/** \file
 * Nexus Channel Core Module (Header)
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 */

#ifndef __NEXUS__CHANNEL__SRC__NEXUS_CHANNEL_CORE_H_
#define __NEXUS__CHANNEL__SRC__NEXUS_CHANNEL_CORE_H_

#include "src/internal_channel_config.h"
#include "src/nexus_channel_om.h"

#if NEXUS_CHANNEL_ENABLED

#define NEXUS_CHANNEL_MAX_RTS_PER_RES 1
#define NEXUS_CHANNEL_NEXUS_DEVICE_ID 0

#include <stdbool.h>

/** Initialize the Nexus Channel module.
 *
 * Called on startup by `nx_core_init()`.
 */
bool nexus_channel_core_init(void);

/** Shutdown the Nexus Channel module.
 *
 * Should be called when the application wishes to safely
 * shut down.
 */
void nexus_channel_core_shutdown(void);

/**Process any pending activity from Nexus channel submodules.
*
* Called inside `nx_core_process()`.
*
* \param seconds_elapsed seconds since this function was previously called
* \return seconds until this function must be called again
*/
uint32_t nexus_channel_core_process(uint32_t seconds_elapsed);

#if NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE
/*! \brief Apply a parsed, valid origin command.
 *
 * Origin commands may create links, delete links, or perform
 * generic accessory targeted or controller targeted actions. These
 * commands come from the Nexus Backend ('Origin') managing this
 * unit.
 *
 * \param om_message message to apply from origin manager
 * \return True if message is applied, false otherwise
 */
bool nexus_channel_core_apply_origin_command(
    const struct nexus_channel_om_command_message* om_message);
#endif /* NEXUS_CHANNEL_SUPPORT_CONTROLLER_MODE */

#endif /* NEXUS_CHANNEL_ENABLED */
#endif
