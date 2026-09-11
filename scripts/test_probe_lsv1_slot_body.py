#!/usr/bin/env python3
"""Unit tests for scripts/probe_lsv1_slot_body.py."""

from __future__ import annotations

import struct
import sys
import tempfile
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from probe_lsv1_slot_body import EPOCH_FILE_HEADER_BYTE_SIZE, probe_slot_file


def _lsv1_payload(format_version: int = 1, flags: int = 1) -> bytes:
    return b"LSV1" + struct.pack("<HH", format_version, flags) + b"\x00" * 8


class ProbeLsv1SlotBodyTests(unittest.TestCase):
    def test_bare_lsv1_at_offset_zero(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "loop_04_00.bin"
            path.write_bytes(_lsv1_payload() + b"SAVE")
            result = probe_slot_file(path)
            self.assertTrue(result["ok"])
            self.assertEqual("lsv1", result["format"])
            self.assertEqual(0, result["payload_offset"])
            self.assertTrue(result["supported"])

    def test_epoch_wrapped_lsv1_at_offset_ten(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "loop_04_00.bin"
            epoch = b"\x86\x08\x00\x00\x01\x00\x3f\x1d\xde\x73"
            self.assertEqual(EPOCH_FILE_HEADER_BYTE_SIZE, len(epoch))
            path.write_bytes(epoch + _lsv1_payload() + b"SAVE")
            result = probe_slot_file(path)
            self.assertTrue(result["ok"])
            self.assertEqual("lsv1", result["format"])
            self.assertEqual(EPOCH_FILE_HEADER_BYTE_SIZE, result["payload_offset"])
            self.assertTrue(result["supported"])

    def test_legacy_without_lsv1_magic(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "loop_04_01.bin"
            path.write_bytes(b"\x86\x08\x00\x00\x01\x00\x00\x00\x00\x00" + b"LEG!" + b"\x00" * 8 + b"SAVE")
            result = probe_slot_file(path)
            self.assertTrue(result["ok"])
            self.assertEqual("legacy", result["format"])


if __name__ == "__main__":
    unittest.main()
