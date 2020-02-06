#!/bin/bash

set -uo pipefail

source buildkite/common.sh

za-init

echo "--- Executing Ceedling unit tests"

# `ceedling test:all` will return nonzero if any tests fail
cd nexus_keycode && ceedling test:all
TESTS_EXIT_STATUS=$?

buildkite-agent artifact upload "build/artifacts/test/report.xml"

echo "--- Preparing unit test coverage report"

# Generate full coverage report and upload it
ceedling gcov:all utils:gcov

buildkite-agent artifact upload "build/artifacts/gcov/**/*"

exit $TESTS_EXIT_STATUS
