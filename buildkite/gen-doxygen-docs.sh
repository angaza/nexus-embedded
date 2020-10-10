#!/bin/bash

set -xuo pipefail

source buildkite/common.sh

za-init

RESULTS_DIR=$ARTIFACTS_DIR/"doxygen"
mkdir -p ${RESULTS_DIR}

RETCODE=0

echo "--- Building Doxygen documentation"

doxygen ./Doxyfile

if [[ $? > 0 ]];
then
    RETCODE=1
fi

cp -r docs/ ${RESULTS_DIR}/

# Exit with a failure if cppcheck fails
exit $RETCODE
