# Executed prior to every build in platformio.ini
# convenience script to set the appropriate
# Kconfig options for Nexus Channel with a single secured link and keycode entry.

import os

# from platformio
Import("env")

FILENAME_USER_CONFIG_H = "lib/nexus/include/user_config.h"
FILENAME_USER_CONFIG_BACKUP_H = "lib/nexus/include/user_config.h.bak"

try:
    # hides existing user config
    os.rename(FILENAME_USER_CONFIG_H, FILENAME_USER_CONFIG_BACKUP_H)
except:
    print("Unable to find old `user_config.h`, safely ignoring")

# Could alternately use platformio tools e.g.
# env.Append(CPPDEFINES=("CONFIG_NEXUS_COMMON_ENABLED", 1))
# except that Nexus code expects `user_config.h` to be present
with open(FILENAME_USER_CONFIG_H, 'w') as f:
    f.write("#define CONFIG_NEXUS_COMMON_ENABLED 1\n")
    f.write("#define CONFIG_NEXUS_CHANNEL_CORE_ENABLED 1\n")
    f.write("#define CONFIG_NEXUS_CHANNEL_LINK_SECURITY_ENABLED 1\n")
    f.write("#define CONFIG_NEXUS_CHANNEL_PLATFORM_ACCESSORY_MODE_SUPPORTED 1\n")
    f.write("#define CONFIG_NEXUS_CHANNEL_MAX_SIMULTANEOUS_LINKS 1\n")
    f.write("#define CONFIG_NEXUS_CHANNEL_USE_PAYG_CREDIT_RESOURCE 1\n")

    # Uncomment these if you wish to enable logging from Nexus Channel Core
    # Zephyr logging console (print statements will be captured)
    #f.write("#define CONFIG_NEXUS_COMMON_OC_PRINT_LOG_ENABLED 1\n")
    #f.write("#define CONFIG_NEXUS_COMMON_OC_DEBUG_LOG_ENABLED 1")
    print("Updated `user_config.h` to a channel security-enabled single link configuration\n")
