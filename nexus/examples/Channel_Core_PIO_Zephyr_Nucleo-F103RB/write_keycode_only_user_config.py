# Executed prior to every build in platformio.ini
# convenience script to set the appropriate
# Kconfig options for 'Channel Core Only' with no user intervention.

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
    f.write("#define CONFIG_NEXUS_KEYCODE_ENABLED 1\n")
    f.write("#define CONFIG_NEXUS_KEYCODE_USE_FULL_KEYCODE_PROTOCOL 1\n")
    f.write("#define CONFIG_NEXUS_KEYCODE_RATE_LIMITING_ENABLED 1\n")
    f.write("#define CONFIG_NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_MAX 128\n")
    f.write("#define CONFIG_NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_BUCKET_INITIAL_COUNT 6\n")
    f.write("#define CONFIG_NEXUS_KEYCODE_PROTOCOL_RATE_LIMIT_REFILL_SECONDS_PER_ATTEMPT 720\n")
    f.write("#define CONFIG_NEXUS_KEYCODE_ENABLE_FACTORY_QC_CODES 1\n")
    f.write("#define CONFIG_NEXUS_KEYCODE_PRO_FACTORY_QC_SHORT_LIFETIME_MAX 5\n")
    f.write("#define CONFIG_NEXUS_KEYCODE_PRO_FACTORY_QC_LONG_LIFETIME_MAX 5\n")
    f.write("#define CONFIG_NEXUS_KEYCODE_PROTOCOL_ENTRY_TIMEOUT_SECONDS 16\n")
    print("Updated `user_config.h` to a Nexus keycode only configuration\n")
