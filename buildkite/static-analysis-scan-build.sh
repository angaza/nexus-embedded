#!/bin/bash

set -uo pipefail

source buildkite/common.sh

za-init

RESULTS_DIR=$ARTIFACTS_DIR/"scan-build"
mkdir -p ${RESULTS_DIR}

LOG_FILENAME_C11="error_logs_c11.txt"
LOG_FILENAME_C11_CLANG="error_logs_c11_clang.txt"
LOG_FILENAME_C99="error_logs_c99.txt"

echo "--- Building stub executable with c11 and gcc"

# Compile a stub build using GCC and c11 standard
export CEEDLING_MAIN_PROJECT_FILE=project.yml
pushd nexus && ceedling clobber release 2> $LOG_FILENAME_C11
cp $LOG_FILENAME_C11 ${RESULTS_DIR}/

echo "--- Building stub executable with c11 and clang"

# Compile a stub build using the clang compiler and c11 standard
export CEEDLING_MAIN_PROJECT_FILE=clang-project.yml
ceedling clobber release 2> $LOG_FILENAME_C11_CLANG
cp $LOG_FILENAME_C11_CLANG ${RESULTS_DIR}/

echo "--- Building stub executable with c99 and gcc"

# https://github.com/ThrowTheSwitch/Ceedling/issues/127 does not always
# seem to work.
# Compile a stub build using GCC and c99 standard
export CEEDLING_MAIN_PROJECT_FILE=c99-project.yml
ceedling clobber release 2> $LOG_FILENAME_C99
cp $LOG_FILENAME_C99 ${RESULTS_DIR}/

# If any errors exist, the logs file will be nonzero size, trigger failure
# via nonzero return value to CI..
if [ -s $LOG_FILENAME_C11 ];
    then exit 1
fi

if [ -s $LOG_FILENAME_C11_CLANG ];
    then exit 1
fi
if [ -s $LOG_FILENAME_C99 ];
    then exit 1
fi
