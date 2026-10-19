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

from hitl.verify.display_window import (
    verify_follow_window_readiness,
    verify_long_loop_display_window,
)

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


def _windowed_args() -> SimpleNamespace:
    return SimpleNamespace(
        record_bars=24,
        hold_track_ms=4000,
        phase_markers=[
            "entered note edit mode",
            "phase:freeze_wait_end",
            "phase:long_press_snap",
            "phase:hold_track_start",
            "phase:hold_track_end",
        ],
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
            _disp_window(50, window_start=6400),
            _disp_window(60, window_start=6656),
            _disp_window(70, window_start=6912),
        ]
        args = SimpleNamespace(
            record_bars=24,
            hold_track_ms=4000,
            phase_markers=[
                "entered note edit mode",
                "phase:freeze_wait_end",
                "phase:long_press_snap",
                "phase:hold_track_start",
                "phase:hold_track_end",
            ],
        )
        result = verify_long_loop_display_window(lines, args)
        self.assertTrue(result["ok"], result.get("issues"))

    def test_ok_sparse_disp_with_indirect_hold(self) -> None:
        """SC_DISP_WINDOW is delta-only; accept play/stop hold release like edit_minimal markers."""
        lines = [
            "Edit session: NOTE_EDIT (Program 1, Note 0 trigger)",
            _disp_window(10, window_start=6302),
            _disp_window(20, window_start=6302),
            "[DEBUG] [BTN] Button released: Ch16 Note40, start=78090, now=82090, duration=4000",
            "[INFO] Button press: Play/Stop (long)",
            "Detailed window centered on playhead",
        ]
        args = SimpleNamespace(
            record_bars=24,
            hold_track_ms=4000,
            phase_markers=[
                "entered note edit mode",
                "phase:long_press_snap",
                "phase:hold_track_start",
                "phase:hold_track_end",
            ],
        )
        result = verify_long_loop_display_window(lines, args)
        self.assertTrue(result["ok"], result.get("issues"))
        self.assertEqual(result.get("hold_verify_mode"), "indirect_button_hold")

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


class PhasePairingTests(unittest.TestCase):
    """A row produced under transport is retained and delivered long after the marker that ends its phase.

    Device time decides which phase a row belongs to; stream position would read a retained row as belonging to
    whatever phase sits where it was delivered.
    """

    def test_a_freeze_row_delivered_below_the_snap_is_read_into_the_freeze(self) -> None:
        """The drained capture's shape: the row produced at 100.05 s arrives at line 2, below the 160 s snap.

        By stream position the freeze interval (between the enter line and the snap line) is empty, so the run falls
        to `freeze_disp_sparse:marker_fallback` and samples whatever retained rows happen to sit there -- whose moved
        windows then read as "not frozen". By device time the freeze reads exactly the row the device produced in it.
        """
        lines = [
            "[100.000] [INFO] [MIDI] Edit session: NOTE_EDIT (Program 1, Note 0 trigger)",
            "[160.000] [INFO] Detailed window centered on playhead",
            _disp_window(100_050_000, window_start=4096),
            _disp_window(160_500_000, window_start=6144),
            _disp_window(160_600_000, window_start=6400),
            _disp_window(160_700_000, window_start=6656),
        ]
        result = verify_long_loop_display_window(lines, _windowed_args())
        self.assertTrue(result["ok"], result.get("issues"))
        self.assertNotIn("freeze_disp_sparse:marker_fallback", result["warnings"])

    def test_a_row_produced_after_the_snap_is_not_read_into_the_freeze(self) -> None:
        """The mirror: a row delivered above the snap but produced after it must not freeze-assert the freeze.

        The freeze is a claim about what the device did between the enter and the snap, so a window the device had
        already moved cannot answer for it. No row was produced inside the freeze here, and the run says so rather
        than passing on a row that was merely delivered where the freeze interval sits.
        """
        lines = [
            "[100.000] [INFO] [MIDI] Edit session: NOTE_EDIT (Program 1, Note 0 trigger)",
            _disp_window(170_000_000, window_start=4352),
            "[160.000] [INFO] Detailed window centered on playhead",
            _disp_window(160_500_000, window_start=6144),
            _disp_window(160_600_000, window_start=6400),
            _disp_window(160_700_000, window_start=6656),
        ]
        result = verify_long_loop_display_window(lines, _windowed_args())
        self.assertFalse(result["ok"], result)
        self.assertTrue(
            any("insufficient_disp_during_note_edit_freeze" in issue for issue in result["issues"]),
            result["issues"],
        )


class FollowWindowReadinessVerifyTests(unittest.TestCase):
    def test_detects_232337_stale_hold_then_heal(self) -> None:
        lines = [
            _disp_window(10, window_start=0, state="PLAYING"),
            (
                "#CAP,54599014,DISP,0,PLAYING,50688,619,2106,619,619,1,5656,16,523"
            ),
            (
                "#CAP,55137543,DISP,0,PLAYING,50688,703,2116,703,703,1,5904,16,703"
            ),
        ]
        result = verify_follow_window_readiness(lines)
        self.assertFalse(result["ok"])
        self.assertTrue(any("stale_follow_window_hold" in issue for issue in result["issues"]))
        self.assertGreaterEqual(result["stale_holds"][0]["hold_ms"], 500.0)

    def test_ok_when_moved_window_stays_aligned(self) -> None:
        lines = [
            "#CAP,1000,DISP,0,PLAYING,50688,756,2105,756,756,1,0,16,756",
            "#CAP,2000,DISP,0,PLAYING,50688,703,2116,703,703,1,5904,16,703",
            "#CAP,3000,DISP,0,PLAYING,50688,704,2116,704,704,1,5920,16,704",
        ]
        result = verify_follow_window_readiness(lines)
        self.assertTrue(result["ok"], result.get("issues"))
        self.assertEqual(result["stale_holds"], [])


if __name__ == "__main__":
    unittest.main()
