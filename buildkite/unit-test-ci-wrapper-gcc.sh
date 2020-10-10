#!/bin/bash

set -uo pipefail

source buildkite/common.sh

za-init


RESULTS_DIR=$ARTIFACTS_DIR/"ceedling-gcc"
mkdir -p ${RESULTS_DIR}

echo "--- Executing Ceedling unit tests with GCC"

# `ceedling test:all` will return nonzero if any tests fail
cd nexus && ceedling verbosity[4] test:all
TESTS_EXIT_STATUS=$?

cp build/artifacts/test/report.xml ${RESULTS_DIR}/

echo "--- Preparing unit test coverage report"

# Generate full coverage report and upload it
ceedling gcov:all utils:gcov

cp -r build/artifacts/gcov ${RESULTS_DIR}/

exit $TESTS_EXIT_STATUS
