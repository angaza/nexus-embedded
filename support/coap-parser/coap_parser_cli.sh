#!/bin/bash

# Nexus CoAP Parser CLI Tool
# (c) 2021 Angaza, Inc.
# This file is released under the MIT license
# The above copyright notice and license shall be included in all copies
# or substantial portions of the Software.

#  `format_input_string` converts the input string into a standardized format
python format_input_string.py "$1" > /tmp/formatted_input.txt &&
echo &&
# if successful, print the formatted input
echo "[Nexus CoAP Parser] formatted input: $(cat /tmp/formatted_input.txt)" &&
echo &&
# if successful, run the C executable on the input to generate the output
./coap-parser-c-cli $(python format_input_string.py "$1") > /tmp/coap_output.txt &&
# and finally print the output
python format_coap_output.py