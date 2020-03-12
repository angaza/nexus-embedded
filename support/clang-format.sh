#!/bin/bash
# Use clang-format to check for formatting/style.
# NOTE: Uses a hidden file, .clang-format, to define repository code style

RETCODE=0

# Version of CLANG_FORMAT to use
# XXX update to 6.0 after updating the buildkite AMI!
CLANG_FORMAT="clang-format-3.9"

PROJECT_NAME="Nexus"
SOURCE_ROOT="../nexus"
SEARCH_PATH="$SOURCE_ROOT/src/ $SOURCE_ROOT/include/ $SOURCE_ROOT/utils/ $SOURCE_ROOT/test $SOURCE_ROOT/examples/desktop_sample_program/src/ $SOURCE_ROOT/examples/desktop_sample_program/inc/"

TOOLPATH=`which $CLANG_FORMAT`

# Ensure the tool exists
if [ -z "$TOOLPATH" ];
then
    printf "Cannot find $CLANG_FORMAT command!\n"
    exit 1
fi

mkdir -p 'tmp-out'

# /src exists for both projects, /inc only for Coprocessor
for file in $(find $SEARCH_PATH \( -iname '*.c' -or -iname '*.h' \));
do
    $CLANG_FORMAT -style=file -output-replacements-xml $file > tmp-out/${file##*/}.xml;
    if grep -c "<replacement " tmp-out/${file##*/}.xml > /dev/null;
    then
        printf "Correcting style errors in $file.\n"
        $CLANG_FORMAT -style=file -i $file;
        # Keep the return code 1 here, so CI continues to catch issues.
        RETCODE=1
    else
        continue
    fi
    printf $"\n\n"
done

rm -rf tmp-out

if [[ $RETCODE -eq 0 ]];
then
    printf "***** No style errors in $PROJECT_NAME. *****\n\n"
fi

exit $RETCODE
