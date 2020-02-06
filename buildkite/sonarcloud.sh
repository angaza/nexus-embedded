#!/bin/bash

source buildkite/common.sh

za-init
za-install-sonar-scanner


cd nexus_keycode

echo "--- Preparing coverage reports for later upload"
# Generate gcov reports for scanner. Will be generated in build/artifacts/gcov
ceedling gcov:all utils:gcov

# ensure we start from a 'clean' state and trigger a full build
ceedling clean

echo "--- Running SonarCloud build wrapper"
$BWRAPPER_BIN --out-dir build_wrapper_output_directory ceedling test:all

# Will find the properties including project key as it is in the same directory
# as `sonar-project.properties`
if [[ $BUILDKITE_PULL_REQUEST_BASE_BRANCH = "master" ]]
then
    # scan all pull request branches which are merging to master
    sonar-scanner -X -Dsonar.pullrequest.base=master \
         -Dsonar.pullrequest.key=$BUILDKITE_PULL_REQUEST \
         -Dsonar.pullrequest.branch=$BUILDKITE_BRANCH
elif [[ $BUILDKITE_BRANCH = "master" ]]
then
    # Also run analysis on master branch, but don't scan any other branches
    sonar-scanner -X
fi
exit $?
