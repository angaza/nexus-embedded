#!/bin/bash

set -xuo pipefail

source buildkite/common.sh

za-init

cd support

OUTFILE=nx_keycode.txt

echo "--- Applying clang-format rules to Nexus Keycode"

# `clang-format.sh` will return nonzero if any errors are detected
./clang-format.sh | tee $OUTFILE
RETCODE=$?

buildkite-agent artifact upload $OUTFILE

exit $RETCODE
