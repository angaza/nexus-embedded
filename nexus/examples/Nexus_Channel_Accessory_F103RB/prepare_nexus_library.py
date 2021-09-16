# Executed prior to every build in platformio.ini
# Copies Nexus Library (nexus/src, nexus/oc, nexus/include, and nexus/utils)
# into the built-in PlatformIO 'lib' folder for this project.

import glob
import os
from shutil import copytree, ignore_patterns, rmtree
from filecmp import dircmp

# from platformio
Import("env")

PROJECT_DIR = env["PROJECT_DIR"]

# "Library name", folder to create within `lib/`
BASE_LIBRARY_FOLDER_NAME = "nexus"

# Folders to copy into BASE_LIBRARY_FOLDER_NAME
NEXUS_LIBRARY_FOLDERS = ["../../src", "../../include", "../../oc", "../../utils"]

# Create folder if its not already there
nexus_pio_lib_path = os.path.join(PROJECT_DIR, "lib", BASE_LIBRARY_FOLDER_NAME)

if not os.path.exists(nexus_pio_lib_path):
    print("Creating directory {}".format(nexus_pio_lib_path))
    os.mkdir(nexus_pio_lib_path)
   
# Overwrite all files in the Nexus folder if changed
for lib_src_folder in NEXUS_LIBRARY_FOLDERS:
    # src, include, oc, utils
    basename = os.path.basename(lib_src_folder)
    copied_folder = os.path.join(nexus_pio_lib_path, basename)

    should_copy = True

    # Delete all '*.o' files before copying, if any exist
    for file_in_dir in os.listdir(lib_src_folder):
        if file_in_dir.endswith(".o"):
            os.remove(os.path.join(lib_src_folder, file_in_dir))

    if os.path.exists(copied_folder):
        dir_cmp_result = dircmp(lib_src_folder, copied_folder)
        # If the folder already exists and there aren't differences, don't copy
        if (
            len(dir_cmp_result.diff_files) == 0 and
            len(dir_cmp_result.right_only) == 0 and
            len(dir_cmp_result.left_only) == 0 and
            len(dir_cmp_result.funny_files) == 0
        ):
            should_copy = False

    if should_copy:
        # PIO `lib` subfolder may exist, but has differences - delete it
        if os.path.exists(copied_folder):
            rmtree(copied_folder)
        # Copy all files from the library source folder into the PIO lib folder
        # Should raise/fail if there is a problem copying
        print("Copying {} into {}".format(lib_src_folder, copied_folder))
        copytree(lib_src_folder, copied_folder)
