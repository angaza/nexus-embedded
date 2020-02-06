#!/bin/bash

set -xuo pipefail

source buildkite/common.sh

za-init

echo "--- Executing Cppcheck against Library"

cd support && ./cppcheck.sh

buildkite-agent artifact upload cppcheck-results.xml
