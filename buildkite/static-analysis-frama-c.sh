#!/bin/bash

source buildkite/common.sh

za-init

# We can't use the pipefail; since we *do* want to continue after a failure,
# so that artifacts are uploaded.
RETCODE=0

RESULTS_FILE="framac-results.txt"
SOURCE_ROOT="nexus"

echo "--- Executing Frama-C wp plugin"

SOURCES=`find $SOURCE_ROOT/src -name '*.c'`

frama-c -wp-rte -wp -wp-prover none \
    -cpp-extra-args=-I$SOURCE_ROOT \
    -cpp-extra-args=-I$SOURCE_ROOT/include \
    -cpp-extra-args=-I$SOURCE_ROOT/src \
    -cpp-extra-args=-I$SOURCE_ROOT/oc \
    -cpp-extra-args=-I$SOURCE_ROOT/oc/api \
    -cpp-extra-args=-I$SOURCE_ROOT/oc/include \
    -cpp-extra-args=-I$SOURCE_ROOT/oc/messaging/coap \
    -cpp-extra-args=-I$SOURCE_ROOT/oc/port \
    -cpp-extra-args=-I$SOURCE_ROOT/oc/util \
    -cpp-extra-args=-I$SOURCE_ROOT/utils \
    $SOURCES > $RESULTS_FILE

# If frama-c fails for any reason, fail the test.
if [[ $? > 0 ]];
then
    RETCODE=1
fi

while read line; do
    # Look for 'incomplete' results from the frama-c test suite
    failed_str=`echo $line | cut -f1 | rev | grep % | rev | grep -v 100%;`
    if [[ ${#failed_str} > 0 ]]; then
        RETCODE=1
    fi
done < $RESULTS_FILE

buildkite-agent artifact upload $RESULTS_FILE

exit $RETCODE
