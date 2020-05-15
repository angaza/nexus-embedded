#!/bin/bash

set -uo pipefail

source buildkite/common.sh

za-init

LOG_FILENAME_C11="error_logs_c11.txt"
LOG_FILENAME_C99="error_logs_c99.txt"

echo "--- Building stub executable with c11"

export CEEDLING_MAIN_PROJECT_FILE=project.yml
pushd nexus && ceedling clobber release 2> $LOG_FILENAME_C11

buildkite-agent artifact upload $LOG_FILENAME_C11

echo "--- Building stub executable with c99"

# Ensure we start 'fresh' for the next build
ceedling clobber

popd
# https://github.com/ThrowTheSwitch/Ceedling/issues/127 does not always
# seem to work.
export CEEDLING_MAIN_PROJECT_FILE=c99-project.yml
cd nexus_keycode && ceedling clobber release 2> $LOG_FILENAME_C99

buildkite-agent artifact upload $LOG_FILENAME_C99

# If any errors exist, the logs file will be nonzero size, trigger failure
# via nonzero return value to CI..
if [ -s $LOG_FILENAME_C11 ];
    then exit 1
fi

if [ -s $LOG_FILENAME_C99 ];
    then exit 1
fi
