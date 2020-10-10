#!/bin/bash

set -xuo pipefail

source buildkite/common.sh

za-init

RESULTS_DIR=$ARTIFACTS_DIR/"Clang-Format"
mkdir -p ${RESULTS_DIR}

cd support

OUTFILE=nx_keycode.txt

echo "--- Checking Clang-Format config"
which clang-format
clang-format --version
clang-format --style=file --dump-config

echo "--- Applying clang-format rules to Nexus Keycode"

# `clang-format.sh` will return nonzero if any errors are detected
./clang-format.sh | tee $OUTFILE
RETCODE=$?

# (don't directly use agent on Docker image)
#buildkite-agent artifact upload $OUTFILE
cp $OUTFILE ${RESULTS_DIR}/

exit $RETCODE
