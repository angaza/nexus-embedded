#!/bin/bash

set -uo pipefail

source buildkite/common.sh

za-init

FULL_BUILD_ERROR_LOG="full_build_error_log.txt"
FULL_NO_RATELIMIT_LOG="full_no_ratelimit_error_log.txt"
SMALLPAD_ERROR_LOG="smallpad_error_log.txt"
CONTROLLER_ERROR_LOG="controller_error_log.txt"
ACCESSORY_ERROR_LOG="accessory_error_log.txt"
MAPFILE_NAME=""

# Use the thumb2 project file
export CEEDLING_MAIN_PROJECT_FILE=thumb2size-project.yml

echo "--- Building stub 'full build' configuration"
#FULL BUILD
# Use default user_config from master branch in repo
pushd nexus && ceedling clobber verbosity[4] release 2> $FULL_BUILD_ERROR_LOG
EXIT_FAILURE=$?
buildkite-agent artifact upload $FULL_BUILD_ERROR_LOG
mv build/artifacts/release/nexus-stub-thumb2.map $MAPFILE_NAME
buildkite-agent artifact upload $MAPFILE_NAME

if [ "$EXIT_FAILURE" -ne 0 ]; then exit $EXIT_FAILURE; fi
MAPFILE_NAME="full_build.map"



echo "--- Computing summary size..."

arm-none-eabi-size build/release/analyzed_keycode_nexus.out
popd

echo "--- Building stub 'full no rate limiting' configuration"
#FULL NO RATELIMIT BUILD
# Manually overwrite Kconfig generated config
cp buildkite/user_config_out_full_no_rate_limit.h nexus/include/user_config.h
pushd nexus && ceedling clobber verbosity[4] release 2> $FULL_NO_RATELIMIT_LOG
EXIT_FAILURE=$?
buildkite-agent artifact upload $FULL_BUILD_ERROR_LOG
mv build/artifacts/release/nexus-stub-thumb2.map $MAPFILE_NAME
buildkite-agent artifact upload $MAPFILE_NAME

if [ "$EXIT_FAILURE" -ne 0 ]; then exit $EXIT_FAILURE; fi
MAPFILE_NAME="full_no_ratelimit.map"

echo "--- Computing summary size..."

arm-none-eabi-size build/release/analyzed_keycode_nexus.out
popd

echo "--- Building stub 'smallpad only (no channel)' configuration"
# SMALLPAD_NO_CHANNEL
# Manually overwrite Kconfig generated config
cp buildkite/user_config_out_smallpad_only.h nexus/include/user_config.h
pushd nexus && ceedling clobber verbosity[4] release 2> $SMALLPAD_ERROR_LOG
EXIT_FAILURE=$?
buildkite-agent artifact upload $SMALLPAD_ERROR_LOG
mv build/artifacts/release/nexus-stub-thumb2.map $MAPFILE_NAME
buildkite-agent artifact upload $MAPFILE_NAME

if [ "$EXIT_FAILURE" -ne 0 ]; then exit $EXIT_FAILURE; fi
MAPFILE_NAME="smallpad.map"

echo "--- Computing summary size..."

arm-none-eabi-size build/release/analyzed_keycode_nexus.out
popd


echo "--- Building stub 'controller only' configuration"
# CONTROLLER
# Manually overwrite Kconfig generated config
cp buildkite/user_config_out_controller_only.h nexus/include/user_config.h
pushd nexus && ceedling clobber verbosity[4] release 2> $CONTROLLER_ERROR_LOG
EXIT_FAILURE=$?
buildkite-agent artifact upload $CONTROLLER_ERROR_LOG
mv build/artifacts/release/nexus-stub-thumb2.map $MAPFILE_NAME
buildkite-agent artifact upload $MAPFILE_NAME

if [ "$EXIT_FAILURE" -ne 0 ]; then exit $EXIT_FAILURE; fi
MAPFILE_NAME="controller.map"

echo "--- Computing summary size..."

arm-none-eabi-size build/release/analyzed_keycode_nexus.out
popd


echo "--- Building stub 'accessory only' configuration"
# ACCESSORY
# Manually overwrite Kconfig generated config
cp buildkite/user_config_out_accessory_only.h nexus/include/user_config.h
pushd nexus && ceedling clobber verbosity[4] release 2> $ACCESSORY_ERROR_LOG
EXIT_FAILURE=$?
buildkite-agent artifact upload $ACCESSORY_ERROR_LOG
mv build/artifacts/release/nexus-stub-thumb2.map $MAPFILE_NAME
buildkite-agent artifact upload $MAPFILE_NAME

if [ "$EXIT_FAILURE" -ne 0 ]; then exit $EXIT_FAILURE; fi
MAPFILE_NAME="accessory.map"


echo "--- Computing summary size..."

arm-none-eabi-size build/release/analyzed_keycode_nexus.out
popd
