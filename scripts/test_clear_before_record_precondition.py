#!/usr/bin/env python3
"""Unit tests for baseline clear-before-record preconditions."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.capture_transitions import (
    _count_capture_transitions,
    _latest_track_state,
    _record_entry_to_recording_count,
)
from host_midi_automation_baseline import (
    _arm_already_satisfied,
    _can_skip_clear_before_record,
    _clear_undo_prune_gate_ok,
    _empty_slot_select_started_recording,
    _serial_has_clear_completed,
    _track_cleared_for_record,
    _transport_running_before_record_arm,
)

class ClearBeforeRecordPreconditionTests(unittest.TestCase):
    def test_skip_when_latest_empty(self) -> None:
        lines = ["#CAP,1,ST,Track,ARMED,EMPTY,0"]
        self.assertTrue(_can_skip_clear_before_record(lines))
        self.assertEqual(_latest_track_state(lines), "EMPTY")

    def test_no_skip_when_no_st_lines_without_positive_empty(self) -> None:
        lines = ["[StorageManager] idle"]
        self.assertFalse(_can_skip_clear_before_record(lines))

    def test_skip_when_clear_ignored(self) -> None:
        lines = ["Clear ignored — track is empty"]
        self.assertTrue(_can_skip_clear_before_record(lines))

    def test_no_skip_when_stopped_with_recs(self) -> None:
        lines = [
            "#CAP,1,ST,Track,RECORDING,STOPPED,0",
            "#CAP,2,RECS,1536,96",
        ]
        self.assertFalse(_can_skip_clear_before_record(lines))

    def test_cleared_for_record_armed_without_content(self) -> None:
        lines = ["#CAP,1,ST,Track,EMPTY,ARMED,0"]
        self.assertTrue(_track_cleared_for_record(lines))

    def test_cleared_for_record_clear_log_without_st(self) -> None:
        lines = ["[INFO] MIDI: Clear Track"]
        self.assertTrue(_track_cleared_for_record(lines))
        self.assertTrue(_serial_has_clear_completed(lines))

    def test_cleared_for_record_selected_slot_clear_log(self) -> None:
        lines = ["[1456.072] [INFO] MIDI: Clear selected slot 2"]
        self.assertTrue(_serial_has_clear_completed(lines))
        self.assertTrue(_track_cleared_for_record(lines))

    def test_empty_slot_select_started_recording_112010(self) -> None:
        """After flash, Loops-row select on an empty slot starts RECORDING.

        [`session_20260909_112010.log`] Loop 1 Start Recording then clear
        `stopToStopped` — seed never reached PLAYING.
        """
        before_select = [
            "#CAP,43301228,ST,Track,STOPPED,PLAYING",
        ]
        after_select = before_select + [
            "#CAP,44684478,ST,Track,STOPPED,RECORDING",
        ]
        after_clear = after_select + [
            "#CAP,45792197,ST,Track,RECORDING,STOPPED_RECORDING",
            "#CAP,45792585,ST,Track,STOPPED_RECORDING,STOPPED",
        ]
        self.assertFalse(_empty_slot_select_started_recording(before_select))
        self.assertTrue(_empty_slot_select_started_recording(after_select))
        self.assertFalse(_empty_slot_select_started_recording(after_clear))

    def test_arm_already_satisfied_ignores_historical_recording_when_stopped(self) -> None:
        """Empty-slot select left RECA in the suffix; clear stopped capture.

        [`session_20260910_115153.log`] arm treated historical RECORDING as
        already satisfied while latest was STOPPED, so no notes were sent.
        """
        lines = [
            "#CAP,1,ST,Track,STOPPED,RECORDING",
            "#CAP,2,RECA,0,399",
            "#CAP,3,ST,Track,RECORDING,STOPPED_RECORDING",
            "#CAP,4,ST,Track,STOPPED_RECORDING,STOPPED",
            "Clear ignored — selected slot is empty",
        ]
        self.assertFalse(_arm_already_satisfied(lines))
        self.assertTrue(
            _arm_already_satisfied(
                lines + ["#CAP,5,ST,Track,STOPPED,ARMED"],
            )
        )
        self.assertTrue(
            _arm_already_satisfied(
                lines + ["#CAP,5,ST,Track,ARMED,RECORDING"],
            )
        )

    def test_transport_not_running_when_stopped_after_clear(self) -> None:
        """After clear STOPPED must not force a transport toggle before arm.

        [`session_20260910_121508.log`] proxy/clock fallback pressed transport
        while STOPPED → PLAYING → arm RECOVERY_FAILED before play→RECORDING.
        """
        lines = [
            "#CAP,1,ST,Track,RECORDING,STOPPED_RECORDING",
            "#CAP,2,ST,Track,STOPPED_RECORDING,STOPPED",
            "Clear ignored — selected slot is empty",
        ]
        self.assertFalse(_transport_running_before_record_arm(lines))
        self.assertTrue(
            _transport_running_before_record_arm(
                lines + ["#CAP,3,ST,Track,STOPPED,PLAYING"],
            )
        )
        self.assertTrue(
            _transport_running_before_record_arm(
                ["Transport started", "#CAP,1,ST,Track,STOPPED,PLAYING"],
            )
        )
        self.assertFalse(
            _transport_running_before_record_arm(
                [
                    "Transport started",
                    "Transport stopped",
                    "#CAP,1,ST,Track,PLAYING,STOPPED",
                ],
            )
        )

    def test_clear_ignored_selected_slot_empty(self) -> None:
        from host_midi_automation_baseline import _serial_has_clear_ignored_empty

        lines = ["Clear ignored — selected slot is empty"]
        self.assertTrue(_serial_has_clear_ignored_empty(lines))
        self.assertTrue(_can_skip_clear_before_record(lines))

    def test_cleared_for_record_clear_log_after_stopped_recs(self) -> None:
        """Long-loop clear: STOPPED+RECS may linger in log; clear log confirms precondition."""
        from host_midi_automation_edit_baseline import _track_cleared_for_record as edit_cleared

        lines = [
            "#CAP,1,ST,Track,RECORDING,STOPPED,0",
            "#CAP,2,RECS,49152,96",
            "[INFO] MIDI: Clear Track",
        ]
        self.assertTrue(edit_cleared(lines))

class RecordTransitionCountTests(unittest.TestCase):
    def test_stopped_to_recording_counts_as_record_arm(self) -> None:
        lines = [
            "#CAP,1,ST,Track,STOPPED,PLAYING",
            "#CAP,2,ST,Track,STOPPED,RECORDING",
            "#CAP,3,ST,Track,RECORDING,STOPPED_RECORDING",
        ]
        counts = _count_capture_transitions(lines)
        self.assertEqual(_record_entry_to_recording_count(counts), 1)
        self.assertEqual(counts.get(("ARMED", "RECORDING"), 0), 0)

class ClearUndoPruneGateTests(unittest.TestCase):
    def test_clear_log_confirmed_exempts_missing_prune(self) -> None:
        self.assertTrue(
            _clear_undo_prune_gate_ok(
                [{"track_index": 4, "result": "clear_log_confirmed"}],
                {"found": False, "remaining_zero": False},
            )
        )

    def test_requires_prune_when_clear_not_exempt(self) -> None:
        self.assertFalse(
            _clear_undo_prune_gate_ok(
                [{"track_index": 4, "result": "no_serial_capture"}],
                {"found": False, "remaining_zero": False},
            )
        )
        self.assertTrue(
            _clear_undo_prune_gate_ok(
                [{"track_index": 4, "result": "no_serial_capture"}],
                {"found": True, "remaining_zero": True},
            )
        )

class EditMinimalSeedOkTests(unittest.TestCase):
    def test_record_only_seed_ignores_playing_transition_fail(self) -> None:
        from hitl.baseline_loop_inventory import base_report_record_seed_ok

        report = {
            "config": {"record_only": True, "edit_record_fixture": True},
            "per_track_stats": [
                {
                    "record_notes_sent": 9,
                    "record_clock_pulses_seen": 192,
                }
            ],
            "assertions": {
                "transition_checks": [
                    {"from": "ARMED", "to": "RECORDING", "ok": True},
                    {"from": "RECORDING", "to": "STOPPED_RECORDING", "ok": True},
                    {"from": "STOPPED_RECORDING", "to": "PLAYING", "ok": False},
                ]
            },
        }
        self.assertTrue(base_report_record_seed_ok(report))

    def test_fixture_seed_ok_without_host_clock_grid(self) -> None:
        from hitl.baseline_loop_inventory import base_report_record_seed_ok

        report = {
            "config": {"record_only": True, "edit_record_fixture": True},
            "per_track_stats": [
                {
                    "record_notes_sent": 8,
                    "record_clock_pulses_seen": 177,
                }
            ],
            "assertions": {
                "transition_checks": [
                    {"from": "ARMED", "to": "RECORDING", "ok": True},
                    {"from": "RECORDING", "to": "STOPPED_RECORDING", "ok": True},
                    {"from": "STOPPED_RECORDING", "to": "PLAYING", "ok": True},
                ]
            },
        }
        self.assertTrue(base_report_record_seed_ok(report))

    def test_edit_minimal_parse_ignores_baseline_loop_slot(self) -> None:
        from hitl.scenarios.edit_minimal import _parse_common_args

        class Args:
            legacy_args = ["--track-number", "5", "--loop-slot", "3", "--record-only"]

        ns = _parse_common_args(Args())
        self.assertEqual(ns.track_number, 5)

if __name__ == "__main__":
    unittest.main()
