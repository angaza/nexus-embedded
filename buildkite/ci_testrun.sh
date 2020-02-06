#!/bin/bash

set -exuo pipefail

# Convenience script to execute all unit tests and quickly determine if any
# failures have occurred.

cd nexus_keycode && ceedling test:all

# Upload failure test report
buildkite-agent artifact upload build/artifacts/test/report.xml
