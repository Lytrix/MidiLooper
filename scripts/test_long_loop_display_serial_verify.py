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


def _disp_window_transition(
    timestamp: int,
    *,
    window_start: int,
    track: int = 0,
    slot: int = 0,
    state: str = "PLAYING",
    note_open: int = 0,
    session: int = 0,
    loop_len: int = LOOP_LEN,
    window_bars: int = 16,
) -> str:
    return (
        f"#CAP,{timestamp},DIAG,disp_window,t={track},s={slot},st={state},ed={note_open},"
        f"es={session},len={loop_len},w={window_start},b={window_bars}"
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


class TransitionReceiptTests(unittest.TestCase):
    """The display window's **transition receipt** (`DIAG,disp_window`) answers the brackets a frozen window hides.

    `SC_DISP` is paint-driven and gated on note counters, so a window that keeps painting the same content raises no
    row: the 2026-10-20 ten-run campaign had a fully green lane run whose drained set carried no `state=PLAYING` `DISP`
    row at all. These tests pin that the receipt is read, is read by **device time**, and does not soften the freeze.
    """

    ENTER = "[100.000] [INFO] [MIDI] Edit session: NOTE_EDIT (Program 1, Note 0 trigger)"
    SNAP = "[160.000] [INFO] Detailed window centered on playhead"

    def test_a_run_without_any_disp_row_is_judged_from_the_receipts(self) -> None:
        lines = [
            self.ENTER,
            self.SNAP,
            _disp_window_transition(99_990_000, window_start=4096),
            _disp_window_transition(160_020_000, window_start=6144),
            _disp_window_transition(160_030_000, window_start=6400),
            _disp_window_transition(160_040_000, window_start=6656),
        ]
        result = verify_long_loop_display_window(lines, _windowed_args())
        self.assertTrue(result["ok"], result.get("issues"))
        self.assertEqual(result["disp_window_transition_row_count"], 4)
        self.assertGreaterEqual(result["playing_bounded_window_rows"], 1)

    def test_the_entry_receipt_sits_at_or_before_the_enter_marker(self) -> None:
        """The receipt is emitted on the device clock; the `Edit session:` acknowledgement the marker follows is later."""
        lines = [
            self.ENTER,
            self.SNAP,
            _disp_window_transition(99_990_000, window_start=4096),
            _disp_window_transition(160_020_000, window_start=6144),
            _disp_window_transition(160_030_000, window_start=6400),
            _disp_window_transition(160_040_000, window_start=6656),
        ]
        result = verify_long_loop_display_window(lines, _windowed_args())
        self.assertTrue(result["ok"], result.get("issues"))
        self.assertIn("freeze_window_rows:entry_anchor_only", result["warnings"])

    def test_a_window_that_moves_inside_the_freeze_still_fails(self) -> None:
        lines = [
            self.ENTER,
            self.SNAP,
            _disp_window_transition(99_990_000, window_start=4096),
            _disp_window_transition(120_000_000, window_start=4352),
            _disp_window_transition(160_020_000, window_start=6144),
            _disp_window_transition(160_030_000, window_start=6400),
            _disp_window_transition(160_040_000, window_start=6656),
        ]
        result = verify_long_loop_display_window(lines, _windowed_args())
        self.assertFalse(result["ok"], result)
        self.assertTrue(
            any("window_not_frozen_during_note_edit" in issue for issue in result["issues"]),
            result["issues"],
        )

    def test_receipts_are_read_in_device_time_order_not_delivery_order(self) -> None:
        """The families are emitted by different owners; a drained capture delivers them interleaved."""
        lines = [
            self.ENTER,
            self.SNAP,
            _disp_window_transition(160_040_000, window_start=6656),
            _disp_window_transition(99_990_000, window_start=4096),
            _disp_window_transition(160_020_000, window_start=6144),
            _disp_window_transition(160_030_000, window_start=6400),
        ]
        result = verify_long_loop_display_window(lines, _windowed_args())
        self.assertTrue(result["ok"], result.get("issues"))
        self.assertNotIn("window_not_frozen_during_note_edit", result["issues"])

    BUTTON_ON = "[157.000] [DEBUG] [MIDI] Button Processor: Ch16 Note40 ON (vel 127)"

    def test_the_hold_inside_the_snap_gesture_is_not_read_as_freeze_drift(self) -> None:
        """The play/stop button is held for 3 s before the long press fires, and the window follows while it is held.

        Measured shape (2026-10-20 rerun): the window holds 4546 through NOTE_EDIT, then 6144 once the button goes down,
        and the long press re-centres on that same 6144. Both facts are correct behaviour; a bracket that ran to the
        long-press *release* would read the hold as freeze drift and the snap as a no-op.
        """
        lines = [
            self.ENTER,
            self.BUTTON_ON,
            self.SNAP,
            _disp_window_transition(99_990_000, window_start=4546),
            _disp_window_transition(157_500_000, window_start=6144),
            _disp_window_transition(160_020_000, window_start=6144),
            _disp_window_transition(161_000_000, window_start=6912),
            _disp_window_transition(162_000_000, window_start=7680),
        ]
        result = verify_long_loop_display_window(lines, _windowed_args())
        self.assertTrue(result["ok"], result.get("issues"))
        self.assertNotIn(
            "long_press_snap_did_not_move_window", " ".join(str(i) for i in result["issues"])
        )

    def test_a_band_row_before_the_entry_is_not_read_as_freeze_drift(self) -> None:
        """Measured shape of the 2026-10-20 second rerun: a bar-band row (3851) 1.5 s before the entry, the entry row
        itself (4401) 11 ms *after* the `Edit session:` acknowledgement. Only the row inside the bracket is the entry."""
        lines = [
            self.ENTER,
            self.BUTTON_ON,
            self.SNAP,
            _disp_window_transition(98_500_000, window_start=3851),
            _disp_window_transition(100_011_000, window_start=4401, note_open=1, session=1),
            _disp_window_transition(157_500_000, window_start=6144, note_open=1, session=1),
            _disp_window_transition(160_020_000, window_start=6144),
            _disp_window_transition(161_500_000, window_start=6912),
            _disp_window_transition(163_000_000, window_start=7680),
        ]
        result = verify_long_loop_display_window(lines, _windowed_args())
        self.assertTrue(result["ok"], result.get("issues"))
        self.assertNotIn("freeze_window_rows:entry_anchor_only", result["warnings"])

    RELEASE = (
        "[163.000] [DEBUG] [BTN] Button released: Ch16 Note40, start=160000, now=164000, duration=4000"
    )

    def test_a_hold_pinned_at_the_window_clamp_accepts_the_release_witness(self) -> None:
        """24 bars -> clamp 18432 - 16 x 768 = 6144 (32 bars -> 12288). Past the clamp the hold has no travel.

        Measured 2026-10-20 (run 06): `play_stop_hold_did_not_track_playhead unique_starts=[6144]` where 6144 is exactly
        `loop_len - window_bars x TICKS_PER_BAR`; the window was doing what it can, so the release witness is the proof.
        """
        lines = [
            self.ENTER,
            self.BUTTON_ON,
            self.SNAP,
            _disp_window_transition(100_011_000, window_start=4401, note_open=1, session=1),
            _disp_window_transition(160_500_000, window_start=6144),
            _disp_window_transition(161_500_000, window_start=6144),
            _disp_window_transition(162_500_000, window_start=6144),
            self.RELEASE,
        ]
        result = verify_long_loop_display_window(lines, _windowed_args())
        self.assertTrue(result["ok"], result.get("issues"))
        self.assertEqual(result.get("hold_verify_mode"), "indirect_button_hold_clamped")
        self.assertIn("hold_window_at_clamp:accepted_play_stop_hold_release", result["warnings"])

    def test_a_hold_pinned_at_the_clamp_without_a_release_still_fails(self) -> None:
        lines = [
            self.ENTER,
            self.BUTTON_ON,
            self.SNAP,
            _disp_window_transition(100_011_000, window_start=4401, note_open=1, session=1),
            _disp_window_transition(160_500_000, window_start=6144),
            _disp_window_transition(161_500_000, window_start=6144),
            _disp_window_transition(162_500_000, window_start=6144),
        ]
        result = verify_long_loop_display_window(lines, _windowed_args())
        self.assertFalse(result["ok"], result)
        self.assertTrue(
            any(
                "play_stop_hold_did_not_track_playhead" in issue
                and "window_at_clamp=1" in issue
                for issue in result["issues"]
            ),
            result["issues"],
        )

    def test_a_static_hold_away_from_the_clamp_keeps_the_original_verdict(self) -> None:
        lines = [
            self.ENTER,
            self.BUTTON_ON,
            self.SNAP,
            _disp_window_transition(100_011_000, window_start=4401, note_open=1, session=1),
            _disp_window_transition(160_500_000, window_start=5000),
            _disp_window_transition(161_500_000, window_start=5000),
            _disp_window_transition(162_500_000, window_start=5000),
            self.RELEASE,
        ]
        result = verify_long_loop_display_window(lines, _windowed_args())
        self.assertFalse(result["ok"], result)
        self.assertTrue(
            any(
                "play_stop_hold_did_not_track_playhead" in issue for issue in result["issues"]
            ),
            result["issues"],
        )

    def test_a_snap_that_never_leaves_the_frozen_window_still_fails(self) -> None:
        lines = [
            self.ENTER,
            self.BUTTON_ON,
            self.SNAP,
            _disp_window_transition(99_990_000, window_start=4546),
            _disp_window_transition(157_500_000, window_start=4546),
            _disp_window_transition(160_020_000, window_start=4546),
            _disp_window_transition(161_000_000, window_start=4546),
        ]
        result = verify_long_loop_display_window(lines, _windowed_args())
        self.assertFalse(result["ok"], result)
        self.assertTrue(
            any("long_press_snap_did_not_move_window" in issue for issue in result["issues"]),
            result["issues"],
        )

    def test_the_count_based_readiness_check_keeps_reading_disp_only(self) -> None:
        """`frame_notes`/`window_note_count` are not carried by the receipt, so it must not answer that check."""
        split_row = _disp_window_transition(1_000_000, window_start=4096)
        result = verify_follow_window_readiness([split_row])
        self.assertEqual(result["playing_moved_window_rows"], 0)


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
