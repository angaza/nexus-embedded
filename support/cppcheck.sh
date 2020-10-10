#!/bin/bash

SOURCE_DIR="../nexus/src"
INCLUDE_DIRS="../nexus/include $SOURCE_DIR"
SOURCES=`find $SOURCE_DIR -name '*.c'`
OUTPUT="cppcheck-results.xml"

OPTIONS="--xml-version=2 --force --inconclusive  \
    --enable=warning,performance,portability --std=c99 --error-exitcode=1 \
    --language=c -I $INCLUDE_DIRS $SOURCES"

echo $OPTIONS
# If errors occur, pipe them to the output.
cppcheck $OPTIONS 2>$OUTPUT
