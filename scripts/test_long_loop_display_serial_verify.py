#!/usr/bin/env python3
"""Unit tests for long-loop display window serial verification."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.verify.display_window import verify_long_loop_display_window

TICKS_PER_BAR = 768
LOOP_LEN = 24 * TICKS_PER_BAR


def _disp_window(
    line_index: int,
    *,
    loop_len: int = LOOP_LEN,
    window_start: int,
    window_bars: int = 16,
    state: str = "PLAYING",
) -> str:
    return (
        f"#CAP,{line_index},DISP,0,{state},{loop_len},32,8,4,32,1,"
        f"{window_start},{window_bars},2"
    )


class LongLoopDisplaySerialVerifyTests(unittest.TestCase):
    def test_ok_full_scenario_markers(self) -> None:
        lines = [
            "MIDI Encoder: Short press - entered note edit mode",
            _disp_window(10, window_start=4096),
            _disp_window(20, window_start=4096),
            _disp_window(30, window_start=4096),
            "Detailed window centered on playhead",
            _disp_window(40, window_start=6144),
            "phase:hold_track_start",
            _disp_window(50, window_start=6400),
            _disp_window(60, window_start=6656),
            _disp_window(70, window_start=6912),
            "phase:hold_track_end",
        ]
        args = SimpleNamespace(record_bars=24)
        result = verify_long_loop_display_window(lines, args)
        self.assertTrue(result["ok"], result.get("issues"))

    def test_fail_when_window_moves_during_freeze(self) -> None:
        lines = [
            "MIDI Encoder: Short press - entered note edit mode",
            _disp_window(10, window_start=4096),
            _disp_window(20, window_start=4096),
            _disp_window(25, window_start=4352),
            "Detailed window centered on playhead",
            _disp_window(40, window_start=6144),
            _disp_window(50, window_start=6400),
            _disp_window(60, window_start=6656),
        ]
        args = SimpleNamespace(record_bars=24)
        result = verify_long_loop_display_window(lines, args)
        self.assertFalse(result["ok"])
        self.assertIn("window_not_frozen_during_note_edit", result["issues"][0])

    def test_fail_when_hold_does_not_track(self) -> None:
        lines = [
            "MIDI Encoder: Short press - entered note edit mode",
            _disp_window(10, window_start=4096),
            _disp_window(20, window_start=4096),
            "Detailed window centered on playhead",
            _disp_window(40, window_start=6144),
            _disp_window(50, window_start=6144),
            _disp_window(60, window_start=6144),
        ]
        args = SimpleNamespace(record_bars=24)
        result = verify_long_loop_display_window(lines, args)
        self.assertFalse(result["ok"])
        self.assertTrue(
            any("play_stop_hold_did_not_track_playhead" in issue for issue in result["issues"])
        )


if __name__ == "__main__":
    unittest.main()
