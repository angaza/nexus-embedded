# Nexus CoAP Parser CLI Tool
# format_coap_output.py
# (c) 2021 Angaza, Inc.
# This file is released under the MIT license.
#
# The above copyright notice and license shall be included in all copies
# or substantial portions of the Software.

def from_dec_to_hex_string(decimal_num):
    return hex(decimal_num).replace("0x", "")


def from_hex_string_to_ascii(hex_str):
    return bytes.fromhex(hex_str).decode("ASCII")


def from_dec_to_hex_to_ascii(decimal_num):
    hex_string = from_dec_to_hex_string(decimal_num)
    return from_hex_string_to_ascii(hex_string)


def convert_coap_response_into_dictionary(coap):
    hex_fields = ["payload"]
    ascii_fields = ["uri_path", "uri_query"]
    list_of_key_values = coap.split("\n")
    d = dict()
    for string in list_of_key_values:
        if len(string) == 0:
            continue
        key, value = string.split(":")
        if key in hex_fields:
            d[key] = value
            continue
        if key in ascii_fields:
            d[key] = from_hex_string_to_ascii(value)
            continue
        d[key] = int(value)
    return d


def assert_presence_of_required_fields(dic, verbose=True):
    required_fields = [
        "version",
        "type",
        "token_len",
        "code",
        "message_id",
        "token",
        "uri_path",
        "content_format",
    ]
    for field in required_fields:
        assert field in dic.keys(), "missing required_field: '{}'".format(field)
        if verbose:
            print("Check ok: required field '{}' is present".format(field))
    return


def assert_type(d, verbose=True):
    type_dict = {
        0: "Confirmable (CON)",
        1: "Non-confirmable (NON)",
        2: "Acknowledgement (ACK)",
        3: "Reset (RST)",
    }
    error = "Invalid message type {}: '{}'; valid type = 1: 'Confirmable (CON)'".format(
        d["type"], type_dict[d["type"]]
    )
    assert d["type"] == 1, error
    if verbose:
        print("Check OK: message type 1: 'Non-confirmable (NON)'")


def assert_version_and_token_len(d, verbose=True):
    assert d["version"] == 1, "version must equal 1; found '{}'".format(d["version"])
    if verbose:
        print("Check Ok: version = 1")
    assert d["token_len"] == 1, "token length must equal 1; found '{}'".format(
        d["token_len"]
    )
    if verbose:
        print("Check Ok: token length = 1")


def validate_dictionary(dictionary, verbose=True):
    assert_presence_of_required_fields(dictionary, verbose)
    assert_type(dictionary, verbose)
    assert_version_and_token_len(dictionary, verbose)


def main():
    with open("/tmp/coap_output.txt", "r") as f:
        coap_response = f.read()

    d = convert_coap_response_into_dictionary(coap_response)
    validate_dictionary(d, verbose=False)

    display_str = "\n"
    for k, v in d.items():
        display_str += str(k) + ": " + str(v) + "\n"
    return display_str


if __name__ == "__main__":
    print(main())
