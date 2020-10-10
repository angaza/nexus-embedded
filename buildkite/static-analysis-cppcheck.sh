#!/bin/bash

set -xuo pipefail

source buildkite/common.sh

za-init

RESULTS_DIR=$ARTIFACTS_DIR/"cppcheck"
mkdir -p ${RESULTS_DIR}

RETCODE=0

echo "--- Executing Cppcheck against Nexus Keycode"

cd support && ./cppcheck.sh

if [[ $? > 0 ]];
then
    RETCODE=1
fi

# (don't directly use agent on Docker image)
#buildkite-agent artifact upload $OUTFILE
cp cppcheck-results.xml ${RESULTS_DIR}/

# Exit with a failure if cppcheck fails
exit $RETCODE
