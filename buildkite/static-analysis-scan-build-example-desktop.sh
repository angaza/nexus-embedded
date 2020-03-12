#!/bin/bash

set -uo pipefail

source buildkite/common.sh

za-init

LOG_FILENAME="error_logs.txt"

echo "--- Building Desktop example program executable"

# Ensure the 'default' build runs without errors
cd nexus/examples/desktop_sample_program && make clean all 2> $LOG_FILENAME

buildkite-agent artifact upload $LOG_FILENAME

# If any errors exist, the logs file will be nonzero size, trigger failure
# via nonzero return value to CI..
if [ -s $LOG_FILENAME ];
    then exit 1
fi
