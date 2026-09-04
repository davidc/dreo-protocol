#!/usr/bin/env python3

import importlib.util
from pathlib import Path
import struct
import sys
import types
import unittest


ROOT = Path(__file__).resolve().parents[2]


class _StopImport(Exception):
    pass


class _SerialStub:
    in_waiting = 0

    def __init__(self, *_args, **_kwargs):
        pass

    def read(self, _size):
        raise _StopImport


serial_stub = types.ModuleType("serial")
serial_stub.Serial = _SerialStub
sys.modules.setdefault("serial", serial_stub)
SPEC = importlib.util.spec_from_file_location("dreo_decode", ROOT / "protocol" / "decode.py")
decode = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = decode
try:
    SPEC.loader.exec_module(decode)
except _StopImport:
    pass

FIXED = "--fixed" in sys.argv


def integer_dp(value: bytes) -> bytes:
    return bytes((1, 1, 2)) + len(value).to_bytes(2, "big") + value


@unittest.skipIf(FIXED, "unmodified-source assertions")
class DecoderBaselineTest(unittest.TestCase):
    def test_one_and_four_byte_integer_positive_controls(self):
        self.assertEqual(decode.parse_dp(integer_dp(b"\x80"))[0].value, -128)
        self.assertEqual(
            decode.parse_dp(integer_dp(b"\x80\x00\x00\x00"))[0].value,
            -(2**31),
        )

    def test_two_byte_integer_is_not_supported_yet(self):
        with self.assertRaises(struct.error):
            decode.parse_dp(integer_dp(b"\x80\x00"))


@unittest.skipUnless(FIXED, "fixed-source assertions")
class DecoderFixedTest(unittest.TestCase):
    def test_signed_integer_boundaries(self):
        cases = (
            (b"\x80", -(2**7)),
            (b"\x00", 0),
            (b"\x7f", 2**7 - 1),
            (b"\x80\x00", -(2**15)),
            (b"\x00\x00", 0),
            (b"\x7f\xff", 2**15 - 1),
            (b"\x80\x00\x00\x00", -(2**31)),
            (b"\x00\x00\x00\x00", 0),
            (b"\x7f\xff\xff\xff", 2**31 - 1),
        )
        for encoded, expected in cases:
            with self.subTest(encoded=encoded.hex()):
                self.assertEqual(decode.parse_dp(integer_dp(encoded))[0].value, expected)

    def test_other_integer_widths_reject_deliberately(self):
        for width in (0, 3, 5):
            with self.subTest(width=width), self.assertRaises(ValueError):
                decode.parse_dp(integer_dp(bytes(width)))


if __name__ == "__main__":
    if "--fixed" in sys.argv:
        sys.argv.remove("--fixed")
    unittest.main()
