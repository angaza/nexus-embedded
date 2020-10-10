#!/bin/bash

set -uo pipefail

source buildkite/common.sh

za-init

RESULTS_DIR=$ARTIFACTS_DIR/"example-program-build-cpp"
mkdir -p ${RESULTS_DIR}

LOG_FILENAME="error_logs.txt"

echo "--- Building Desktop example program in C++"

# Ensure the 'default' build runs without errors
cd nexus/examples/desktop_sample_program && make clean all BUILD=cpp 2> $LOG_FILENAME

cp $LOG_FILENAME ${RESULTS_DIR}/

# If any errors exist, the logs file will be nonzero size, trigger failure
# via nonzero return value to CI..
if [ -s $LOG_FILENAME ];
    then exit 1
fi

# Run the expect scripts in a blocking way (should work on CPP build as well)

echo "--- Running desktop sample program expect script - keycode (C++)"
rm -f test_nv_file.nv  # must not be present prior to test
RETCODE=0
./expect_scripts/sample_program_keycode_test

if [[ $? > 0 ]];
then
    RETCODE=1
fi

echo "--- Running desktop sample program expect script - channel (C++)"
rm -f test_nv_file.nv  # must not be present prior to test
./expect_scripts/sample_program_channel_test

if [[ $? > 0 ]];
then
    RETCODE=1
fi

# Exit with failure if expect scripts fail
exit $RETCODE
