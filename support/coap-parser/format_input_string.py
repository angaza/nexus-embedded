# Nexus CoAP Parser CLI Tool
# format_input_string.py
# (c) 2021 Angaza, Inc.
# This file is released under the MIT license.
#
# The above copyright notice and license shall be included in all copies
# or substantial portions of the Software.

import sys

def format_single_hex_str(string):
    assert type(string) == str, f"input should be a string; {type(string)} was passed"
    assert len(string) > 0, "bytestring (hex) should not be empty"

    result = hex(int(string, 16)).replace("0x", "")
    if len(result) == 1:
        return "0" + result
    return result


def main(input_str):
    """
    :param input_str: str of hexadecimal values,
                      e.g.: "0x51,01,0x0 4,0x8A,0xB4,0x62, 0x61, 74,   0x74"
    :return: str of hexadecimal values separated by spaces (no commas) with hex notation
             stripped ('8a' instead of '0x8a'), lowercased ('8a' instead of '8A')
             and zero padded if only one character ('03' instead of '3')
                      e.g.: "51 01 00 04 8a b4 62 61 74 74"
    """
    values_no_format = input_str.replace(",", " ").split()  # list of strings
    return " ".join([format_single_hex_str(e) for e in values_no_format])


if __name__ == "__main__":
    input_string = sys.argv[1]
    print(main(input_string))
