#!/usr/bin/env python3
"""Unit tests for live-record display serial verification (D1 regression gate)."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from host_midi_automation_edit_baseline import _verify_live_record_display


def _disp(loop_len: int, take: int, frame: int, *, visual: int = 0, buf: int = 1, pub: int = 0) -> str:
    return f"#CAP,1,DISP,0,RECORDING,{loop_len},{take},{visual},{frame},{buf},{pub}"


class LiveRecordDisplaySerialVerifyTests(unittest.TestCase):
    def test_ok_when_frame_notes_appear_during_record(self) -> None:
        lines = [
            "#CAP,1,RECA,slot=0",
            _disp(16, 1, 1),
            _disp(48, 2, 2),
            _disp(96, 4, 4),
            ",ST,Track,RECORDING,STOPPED_RECORDING",
        ]
        result = _verify_live_record_display(lines)
        self.assertTrue(result["ok"])
        self.assertGreater(result["frame_positive_samples"], 0)

    def test_fail_sustained_frame_zero_while_capture_grows(self) -> None:
        lines = [
            "#CAP,1,RECA,slot=0",
            _disp(48, 1, 0),
            _disp(58, 1, 0),
            _disp(68, 1, 0),
            _disp(78, 1, 0),
            _disp(88, 1, 0),
            _disp(98, 2, 0),
            ",ST,Track,RECORDING,STOPPED_RECORDING",
        ]
        result = _verify_live_record_display(lines)
        self.assertFalse(result["ok"])
        self.assertTrue(
            any(str(i).startswith("live_record_display:sustained_frame_zero:") for i in result["issues"])
        )
        self.assertGreaterEqual(result["max_sustained_frame_zero"], 5)

    def test_fail_when_no_frame_notes_for_entire_record_window(self) -> None:
        lines = [
            "#CAP,1,RECA,slot=0",
            _disp(48, 1, 0),
            _disp(96, 2, 0),
            ",ST,Track,RECORDING,STOPPED_RECORDING",
        ]
        result = _verify_live_record_display(lines)
        self.assertFalse(result["ok"])
        self.assertIn("live_record_display:no_frame_notes_during_record", result["issues"])

    def test_reca_missing(self) -> None:
        result = _verify_live_record_display([_disp(48, 1, 1)])
        self.assertFalse(result["ok"])
        self.assertIn("live_record_display:reca_missing", result["issues"])


if __name__ == "__main__":
    unittest.main()
