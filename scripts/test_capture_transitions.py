#!/usr/bin/env python3
"""Unit tests for HITL capture transition helpers."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.capture_transitions import (
    _extract_recs_lengths_from_human_logs,
    _merge_transition_counts_with_evidence,
    _serial_capture_sparse_for_verification,
    _serial_has_armed_after,
    _serial_has_overdubbing_after,
    _serial_has_playing_after_record_stop,
    _serial_has_recording_active,
)
from hitl.legacy_record_baseline import (
    _can_skip_clear_before_record,
    _evaluate_clear_precondition,
    _serial_has_clear_aborted,
    _track_cleared_for_record,
)
from hitl.serial_collector import serial_line_resets_heartbeat


class CaptureTransitionEvidenceTests(unittest.TestCase):
    def test_armed_detects_human_log(self) -> None:
        lines = [
            "[46.922] [INFO] [TRACK] Track 4 armed, waiting for clock to start recording",
        ]
        self.assertTrue(_serial_has_armed_after(lines))

    def test_recording_active_detects_human_log(self) -> None:
        lines = [
            "[49.573] [DEBUG] [TRACK] Recording started @ tick 0 (startLoopTick=0 loopStart=0)",
        ]
        self.assertTrue(_serial_has_recording_active(lines))

    def test_arm_message_is_not_recording_active(self) -> None:
        lines = [
            "[951.469] [INFO] Loop 2: Start Recording",
            "[951.469] [INFO] [TRACK] Track 4 armed, waiting for clock to start recording",
        ]
        self.assertFalse(_serial_has_recording_active(lines))

    def test_recording_detected_when_baseline_before_started_line(self) -> None:
        lines = [
            "bootstrap",
            "transport armed",
            "[952.174] [DEBUG] [TRACK] Recording started @ tick 0 (startLoopTick=0 loopStart=0)",
        ]
        self.assertTrue(_serial_has_recording_active(lines, after_index=2))

    def test_overdubbing_detects_human_log_after_baseline(self) -> None:
        lines = [
            "bootstrap",
            "[1179.380] [INFO] MIDI Button A: Live Overdub",
            "[1179.380] [DEBUG] [TRACK] Overdubbing started @ tick 520",
        ]
        self.assertTrue(_serial_has_overdubbing_after(lines, after_index=1))

    def test_overdubbing_not_detected_before_baseline(self) -> None:
        lines = [
            "[1179.380] [DEBUG] [TRACK] Overdubbing started @ tick 520",
            "new press",
        ]
        self.assertFalse(_serial_has_overdubbing_after(lines, after_index=1))

    def test_playing_after_stop_requires_both_markers(self) -> None:
        stop_only = ["[57.931] [DEBUG] [TRACK] Recording stopped @ tick 2704"]
        self.assertFalse(_serial_has_playing_after_record_stop(stop_only))
        both = stop_only + ["[57.947] [DEBUG] [TRACK] Playback started @ tick 2704"]
        self.assertTrue(_serial_has_playing_after_record_stop(both))

    def test_merge_transition_counts_uses_human_logs_when_st_missing(self) -> None:
        lines = [
            "#CAP,1,ST,Track,OVERDUBBING,PLAYING",
            "[1179.380] [DEBUG] [TRACK] Overdubbing started @ tick 520",
            "[1185.120] [DEBUG] [TRACK] Overdubbing stopped @ tick 1000",
        ]
        merged = _merge_transition_counts_with_evidence(lines)
        self.assertEqual(merged.get(("PLAYING", "OVERDUBBING"), 0), 1)
        self.assertEqual(merged.get(("OVERDUBBING", "PLAYING"), 0), 1)

    def test_recs_lengths_from_human_stop_line(self) -> None:
        lines = [
            "[1840.889] [INFO] Record stop truncation rewind: raw=1827 final=1536 playbackTick=291",
            "[1840.890] [DEBUG] [TRACK] Recording stopped @ tick 291 (recStart=0 length=1536)",
        ]
        rows = _extract_recs_lengths_from_human_logs(lines)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["final_length"], 1536)
        self.assertEqual(rows[0]["raw_length"], 1827)

    def test_serial_capture_sparse_when_cap_markers_missing(self) -> None:
        lines = [
            "[1836.133] [DEBUG] [TRACK] Recording started @ tick 0",
            "[1840.890] [DEBUG] [TRACK] Recording stopped @ tick 291 (recStart=0 length=1536)",
        ]
        self.assertTrue(_serial_capture_sparse_for_verification(lines))


class ClearPreconditionTests(unittest.TestCase):
    def test_explicit_loop_slot_never_skips_clear(self) -> None:
        lines = ["[info] Clear ignored — selected slot is empty"]
        self.assertFalse(_can_skip_clear_before_record(lines, loop_slot=2))

    def test_stale_clear_ignored_outside_track_window_does_not_skip(self) -> None:
        lines = [
            "[info] Clear ignored — selected slot is empty",
            "#CAP,1,ST,Track,STOPPED,PLAYING",
        ]
        self.assertFalse(_can_skip_clear_before_record(lines, after_index=1))

    def test_clear_aborted_is_not_satisfied(self) -> None:
        lines = ["[6.853] [ERROR] Clear aborted: could not complete deferred save first"]
        self.assertTrue(_serial_has_clear_aborted(lines))
        satisfied, code = _evaluate_clear_precondition(lines, loop_slot=2, after_index=0)
        self.assertFalse(satisfied)
        self.assertEqual(code, "clear_aborted")

    def test_stopped_with_disp_length_not_cleared(self) -> None:
        lines = [
            "#CAP,1,ST,Track,PLAYING,STOPPED",
            "#CAP,2,DISP,1,STOPPED,3072,57,57,57,57,1",
        ]
        self.assertFalse(_track_cleared_for_record(lines, loop_slot=2))


class SerialHeartbeatNoiseTests(unittest.TestCase):
    def test_dframe_does_not_reset_heartbeat(self) -> None:
        self.assertFalse(serial_line_resets_heartbeat("#CAP,1,DFRAME,21,9254,90"))

    def test_state_line_resets_heartbeat(self) -> None:
        self.assertTrue(serial_line_resets_heartbeat("#CAP,1,ST,Track,STOPPED,ARMED"))


if __name__ == "__main__":
    unittest.main()
