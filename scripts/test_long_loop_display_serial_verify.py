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

from hitl.scenarios.long_loop_display_window import (
    _ensure_note_edit_exited,
    _parse_common_args,
    latest_edit_session_mode,
    note_edit_repress_needed,
    record_press_needs_retry,
    record_stop_needs_retry,
)
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

    def test_display_hold_defaults_to_none(self) -> None:
        """The adoption hold is opt-in: an ordinary run must not silently run a longer transport."""
        ns = _parse_common_args(SimpleNamespace(legacy_args=[]))
        self.assertEqual(ns.display_hold_s, 0.0)
        self.assertEqual(ns.adoption_hold_s, 0.0)

    def test_adoption_hold_is_forwarded_through_the_legacy_args(self) -> None:
        """The opening playback hold is a separate knob: it must not be confused with the display hold."""
        ns = _parse_common_args(SimpleNamespace(legacy_args=["--adoption-hold-s", "90"]))
        self.assertEqual(ns.adoption_hold_s, 90.0)
        self.assertEqual(ns.display_hold_s, 0.0)

    def test_display_hold_is_forwarded_through_the_legacy_args(self) -> None:
        """The preset's own parser does not know the flag, so the runner hands it down as a legacy arg."""
        ns = _parse_common_args(SimpleNamespace(legacy_args=["--record-bars", "32", "--display-hold-s", "90"]))
        self.assertEqual(ns.record_bars, 32)
        self.assertEqual(ns.display_hold_s, 90.0)

    def test_a_record_press_is_repeated_only_when_the_device_is_not_recording(self):
        """A lagging capture log must not turn an unobserved press into a second (stopping) press.

        Measured 2026-10-20: the retry stopped a take that had started, leaving a one-bar loop and making every
        downstream display check fail for a reason that had nothing to do with the window.
        """
        self.assertFalse(record_press_needs_retry("RECORDING"))
        self.assertTrue(record_press_needs_retry(None))
        self.assertTrue(record_press_needs_retry("PLAYING"))
        self.assertTrue(record_press_needs_retry("STOPPED"))

    def test_a_record_stop_is_repeated_only_while_the_take_is_still_recording(self):
        """The mirror trap: re-pressing a stop that already landed is a new record gesture, not a stop.

        Measured 2026-10-20 as a transport stop right after the take reached its length; the NOTE_EDIT phases then ran
        with the transport stopped and the playing-window rows never arrived.
        """
        self.assertTrue(record_stop_needs_retry("RECORDING"))
        self.assertFalse(record_stop_needs_retry("STOPPED_RECORDING"))
        self.assertFalse(record_stop_needs_retry("PLAYING"))
        self.assertFalse(record_stop_needs_retry(None))

    def test_latest_edit_session_mode_reads_the_last_ack(self):
        self.assertIsNone(latest_edit_session_mode(["nothing here"]))
        lines = [
            "Edit session: LOOP_EDIT (Program 2, Note 100 trigger)",
            "Edit session: NOTE_EDIT (Program 1, Note 0 trigger)",
        ]
        self.assertEqual(latest_edit_session_mode(lines), "NOTE_EDIT")
        self.assertEqual(latest_edit_session_mode(list(reversed(lines))), "LOOP_EDIT")

    def test_an_entry_press_is_repeated_only_after_a_fresh_loop_edit_ack(self):
        """Without an ack the mode is unknown; a blind re-press would cycle an unobserved NOTE_EDIT back to LOOP_EDIT."""
        self.assertTrue(note_edit_repress_needed("LOOP_EDIT"))
        self.assertFalse(note_edit_repress_needed("NOTE_EDIT"))
        self.assertFalse(note_edit_repress_needed(None))

    def test_note_edit_exit_presses_only_from_note_edit_and_waits_for_the_loop_ack(self):
        lines = ["Edit session: NOTE_EDIT (Program 1, Note 0 trigger)"]
        port = _FakeOutPort(on_send=lambda msg: lines.append(
            "Edit session: LOOP_EDIT (Program 2, Note 100 trigger)"
        ) if msg.type == "note_on" else None)
        ok = _ensure_note_edit_exited(
            port, _FakeCollector(lines), press_ms=1, timeout_s=1.0
        )
        self.assertTrue(ok)
        self.assertEqual([msg.type for msg in port.messages], ["note_on", "note_off"])

    def test_note_edit_exit_is_a_no_op_when_the_overlay_is_already_closed(self):
        lines = ["Edit session: LOOP_EDIT (Program 2, Note 100 trigger)"]
        port = _FakeOutPort()
        self.assertTrue(_ensure_note_edit_exited(port, _FakeCollector(lines), press_ms=1, timeout_s=1.0))
        self.assertEqual(port.messages, [])

    def test_note_edit_exit_refuses_when_the_mode_is_unknown(self):
        port = _FakeOutPort()
        self.assertFalse(_ensure_note_edit_exited(port, _FakeCollector([]), press_ms=1, timeout_s=0.1))
        self.assertEqual(port.messages, [])

    def test_note_edit_exit_never_presses_twice_on_a_lagging_ack(self):
        """A lagging ack must not become a return trip into NOTE_EDIT: one press, then an unconfirmed exit."""
        lines = ["Edit session: NOTE_EDIT (Program 1, Note 0 trigger)"]
        port = _FakeOutPort()
        self.assertFalse(_ensure_note_edit_exited(port, _FakeCollector(lines), press_ms=1, timeout_s=0.1))
        self.assertEqual([msg.type for msg in port.messages], ["note_on", "note_off"])


class _FakeOutPort:
    def __init__(self, on_send=None) -> None:
        self.messages = []
        self._on_send = on_send

    def send(self, message) -> None:
        self.messages.append(message)
        if self._on_send is not None:
            self._on_send(message)


class _FakeCollector:
    def __init__(self, lines) -> None:
        self._lines = lines

    def snapshot(self):
        return list(self._lines)


if __name__ == "__main__":
    unittest.main()
