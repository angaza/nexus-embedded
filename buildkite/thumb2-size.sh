#!/bin/bash

set -uo pipefail

source buildkite/common.sh

za-init

RESULTS_DIR=$ARTIFACTS_DIR/"cppcheck"
mkdir -p ${RESULTS_DIR}


FULL_BUILD_ERROR_LOG="full_build_error_log.txt"
FULL_NO_RATELIMIT_LOG="full_no_ratelimit_error_log.txt"
SMALLPAD_ERROR_LOG="smallpad_error_log.txt"
CONTROLLER_ERROR_LOG="controller_error_log.txt"
ACCESSORY_ERROR_LOG="accessory_error_log.txt"
CHANNEL_CORE_ONLY_ERROR_LOG="channel_core_only_error_log.txt"

# Use the thumb2 project file
export CEEDLING_MAIN_PROJECT_FILE=thumb2size-project.yml

echo "--- Building stub 'full build' configuration"
#FULL BUILD
# Use default user_config from master branch in repo
pushd nexus && ceedling clobber verbosity[4] release 2> $FULL_BUILD_ERROR_LOG
EXIT_FAILURE=$?
MAPFILE_NAME="full_build.map"

cp $FULL_BUILD_ERROR_LOG ${RESULTS_DIR}/
mv build/artifacts/release/nexus-stub-thumb2.map $MAPFILE_NAME
cp $MAPFILE_NAME ${RESULTS_DIR}/

if [ "$EXIT_FAILURE" -ne 0 ]; then exit $EXIT_FAILURE; fi

echo "--- Computing summary size..."

arm-none-eabi-size build/release/analyzed_nexus.out
popd

echo "--- Building stub 'full no rate limiting' configuration"
#FULL NO RATELIMIT BUILD
# Manually overwrite Kconfig generated config
cp buildkite/user_config_out_full_no_rate_limit.h nexus/include/user_config.h
pushd nexus && ceedling clobber verbosity[4] release 2> $FULL_NO_RATELIMIT_LOG
EXIT_FAILURE=$?
MAPFILE_NAME="full_no_ratelimit.map"

cp $FULL_NO_RATELIMIT_LOG ${RESULTS_DIR}/
mv build/artifacts/release/nexus-stub-thumb2.map $MAPFILE_NAME
cp $MAPFILE_NAME ${RESULTS_DIR}/

if [ "$EXIT_FAILURE" -ne 0 ]; then exit $EXIT_FAILURE; fi

echo "--- Computing summary size..."

arm-none-eabi-size build/release/analyzed_nexus.out
popd

echo "--- Building stub 'smallpad only (no channel)' configuration"
# SMALLPAD_NO_CHANNEL
# Manually overwrite Kconfig generated config
cp buildkite/user_config_out_smallpad_only.h nexus/include/user_config.h
pushd nexus && ceedling clobber verbosity[4] release 2> $SMALLPAD_ERROR_LOG
EXIT_FAILURE=$?
MAPFILE_NAME="smallpad.map"

cp $SMALLPAD_ERROR_LOG ${RESULTS_DIR}/
mv build/artifacts/release/nexus-stub-thumb2.map $MAPFILE_NAME
cp $MAPFILE_NAME ${RESULTS_DIR}/

if [ "$EXIT_FAILURE" -ne 0 ]; then exit $EXIT_FAILURE; fi

echo "--- Computing summary size..."

arm-none-eabi-size build/release/analyzed_nexus.out
popd


echo "--- Building stub 'controller only' configuration"
# CONTROLLER
# Manually overwrite Kconfig generated config
cp buildkite/user_config_out_controller_only.h nexus/include/user_config.h
pushd nexus && ceedling clobber verbosity[4] release 2> $CONTROLLER_ERROR_LOG
EXIT_FAILURE=$?
MAPFILE_NAME="controller.map"

cp $CONTROLLER_ERROR_LOG ${RESULTS_DIR}/
mv build/artifacts/release/nexus-stub-thumb2.map $MAPFILE_NAME
cp $MAPFILE_NAME ${RESULTS_DIR}/

if [ "$EXIT_FAILURE" -ne 0 ]; then exit $EXIT_FAILURE; fi

echo "--- Computing summary size..."

arm-none-eabi-size build/release/analyzed_nexus.out
popd


echo "--- Building stub 'accessory only' configuration"
# ACCESSORY
# Manually overwrite Kconfig generated config
cp buildkite/user_config_out_accessory_only.h nexus/include/user_config.h
pushd nexus && ceedling clobber verbosity[4] release 2> $ACCESSORY_ERROR_LOG
EXIT_FAILURE=$?
MAPFILE_NAME="accessory.map"

cp $ACCESSORY_ERROR_LOG ${RESULTS_DIR}/
mv build/artifacts/release/nexus-stub-thumb2.map $MAPFILE_NAME
cp $MAPFILE_NAME ${RESULTS_DIR}/

if [ "$EXIT_FAILURE" -ne 0 ]; then exit $EXIT_FAILURE; fi

echo "--- Computing summary size..."

arm-none-eabi-size build/release/analyzed_nexus.out
popd


echo "--- Building stub 'channel core only' configuration"
# CHANNEL CORE ONLY (no accessory/controller linking)
# Manually overwrite Kconfig generated config
cp buildkite/user_config_out_channel_core_only.h nexus/include/user_config.h
pushd nexus && ceedling clobber verbosity[4] release 2> $CHANNEL_CORE_ONLY_ERROR_LOG
EXIT_FAILURE=$?
MAPFILE_NAME="channel_core_only.map"

cp $CHANNEL_CORE_ONLY_ERROR_LOG ${RESULTS_DIR}/
mv build/artifacts/release/nexus-stub-thumb2.map $MAPFILE_NAME
cp $MAPFILE_NAME ${RESULTS_DIR}/

if [ "$EXIT_FAILURE" -ne 0 ]; then exit $EXIT_FAILURE; fi

echo "--- Computing summary size..."

arm-none-eabi-size build/release/analyzed_nexus.out
popd

