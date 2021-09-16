from format_input_string import main as format_input_string


def test_input_output_pair(input_str, output_str):
    assert format_input_string(input_str) == output_str
    print(f"Check OK: formatted '{input_str}' = '{output_str}'")


def main(input_output_pairs):
    assert type(input_output_pairs) == list, f"Input should be a list"
    for e in input_output_pairs:
        assert type(e) == tuple, f"elements in list should be tuples; found '{e}'"
        test_input_output_pair(e[0], e[1])
    print(f"{len(input_output_pairs)} tests passed successfully")


if __name__ == "__main__":
    pairs = [
        ("51 01 00 04 8a b4 62 61 74 74", "51 01 00 04 8a b4 62 61 74 74"),
        (
            "0x51 0x01 0x00 0x04 0x8a 0xb4 0x62 0x61 0x74 0x74",
            "51 01 00 04 8a b4 62 61 74 74"
        ),
        ("51, 01, 00, 04, 8a, b4, 62, 61, 74, 74", "51 01 00 04 8a b4 62 61 74 74"),
        ("51 01 00 04 8A B4 62 61 74 74", "51 01 00 04 8a b4 62 61 74 74"),
        ("51 1 0 4 8a b4 62 61 74 74", "51 01 00 04 8a b4 62 61 74 74"),
        (
            "0x51,01,0x0 4,0x8A,0xB4,0x62, 0x61, 74,   0x74",
            "51 01 00 04 8a b4 62 61 74 74"
        ),
    ]
    main(pairs)
