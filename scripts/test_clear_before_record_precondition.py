#!/usr/bin/env python3
"""Unit tests for baseline clear-before-record preconditions."""

from __future__ import annotations

import sys
import unittest
from unittest.mock import Mock, patch
from types import SimpleNamespace
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl import legacy_record_baseline as baseline

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
    _evaluate_clear_precondition,
    _recording_currently_active,
    _selected_slot_disp_empty,
    _selected_slot_provably_empty,
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

    def test_empty_slot_select_recording_via_disp_without_st(self) -> None:
        """DISP RECORDING is enough when ST has not arrived yet.

        [`session_20260911_092650.log`] empty Loop-1 select flooded DISP
        RECORDING; ST-only checks missed the ghost capture.
        """
        before = [
            "#CAP,10070618,DISP,0,STOPPED,0,0,0,0,0,0,0,16,0",
        ]
        after_disp = before + [
            "#CAP,17019208,DISP,0,RECORDING,1,0,0,0,0,0,0,1,0",
        ]
        self.assertFalse(_recording_currently_active(before))
        self.assertFalse(_empty_slot_select_started_recording(before))
        self.assertTrue(_recording_currently_active(after_disp))
        self.assertTrue(_empty_slot_select_started_recording(after_disp))

    def test_clear_log_confirmed_without_empty_st_092650(self) -> None:
        """Successful clear of ghost RECORDING ends STOPPED without EMPTY ST.

        [`session_20260911_092650.log`] Track cleared + Clear selected slot 1;
        host previously timed out because STOPPED was treated as clear disproof
        and follow lagged past the clear logs.
        """
        baseline = [
            "#CAP,16978483,ST,Track,STOPPED,RECORDING",
            "#CAP,17019208,DISP,0,RECORDING,1,0,0,0,0,0,0,1,0",
        ]
        after_clear = baseline + [
            "[20.456] [DEBUG] [TRACK] Track cleared @ tick 1885",
            "[20.456] [INFO] MIDI: Clear selected slot 1",
            "#CAP,21000000,ST,Track,RECORDING,STOPPED",
            "#CAP,21000100,DISP,0,STOPPED,0,0,0,0,0,0,0,1,0",
        ]
        satisfied, result = _evaluate_clear_precondition(
            after_clear, loop_slot=1, after_index=len(baseline)
        )
        self.assertTrue(satisfied)
        self.assertEqual(result, "clear_log_confirmed")
        self.assertFalse(_recording_currently_active(after_clear))
        self.assertFalse(
            _empty_slot_select_started_recording(
                after_clear, after_index=len(baseline)
            )
        )

    def test_selected_slot_empty_after_ghost_record_stop_093852(self) -> None:
        """After select-started RECORDING stop, DISP length 0 means skip clear.

        [`session_20260911_093852.log`] clear long-press hit PERS,drain,stuck
        while the slot was already empty.
        """
        after_select = [
            "#CAP,737554413,ST,Track,STOPPED,RECORDING",
            "#CAP,737604237,DISP,0,RECORDING,18,0,0,0,0,0,0,1,0",
        ]
        after_stop = after_select + [
            "#CAP,738171000,ST,Track,RECORDING,STOPPED_RECORDING",
            "#CAP,738171277,ST,Track,STOPPED_RECORDING,STOPPED",
            "#CAP,738211245,DISP,0,STOPPED,0,0,0,0,0,0,0,16,0",
        ]
        self.assertFalse(
            _selected_slot_disp_empty(after_select, loop_slot=1, after_index=0)
        )
        self.assertTrue(
            _selected_slot_disp_empty(after_stop, loop_slot=1, after_index=0)
        )

    def test_provably_empty_accepts_state_edge_before_slot_disp_row(self) -> None:
        """ST EMPTY proves the selected slot empty before its DISP row catches up.

        [`sevt-fix-verify4`]: ``ST,Track,STOPPED_RECORDING,EMPTY`` at 35506792us, the
        slot DISP ``DISP,0,EMPTY,0`` only at 35730735us. Reading DISP alone saw the
        stale RECORDING length (1201), pressed clear, failed to confirm EMPTY, and
        aborted setup before any genuine record.
        """
        lines = [
            "#CAP,32514859,ST,Track,STOPPED,RECORDING",
            "#CAP,35457622,DISP,0,RECORDING,1201,0,0,0,0,0,0,2,0",
            "#CAP,35506426,ST,Track,RECORDING,STOPPED_RECORDING",
            "#CAP,35506792,ST,Track,STOPPED_RECORDING,EMPTY",
        ]
        self.assertFalse(_selected_slot_disp_empty(lines, loop_slot=1, after_index=0))
        self.assertTrue(
            _selected_slot_provably_empty(lines, loop_slot=1, after_index=0)
        )
        # A slot with committed content is never skipped.
        content = [
            "#CAP,1,ST,Track,STOPPED,PLAYING",
            "#CAP,2,DISP,0,PLAYING,1536,0,0,0,0,0,0,2,0",
        ]
        self.assertFalse(
            _selected_slot_provably_empty(content, loop_slot=1, after_index=0)
        )
        # No target slot selected: never skip.
        self.assertFalse(
            _selected_slot_provably_empty(lines, loop_slot=0, after_index=0)
        )

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
        from host_midi_automation_baseline import _transport_running_before_record_arm

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
        # Clear leaves STOPPED while sequencer still runs (no Transport started
        # line in capture) — must still stop before arm.
        self.assertTrue(
            _transport_running_before_record_arm(
                [
                    "#CAP,1,ST,Track,STOPPED,PLAYING",
                    "#CAP,2,ST,Track,PLAYING,STOPPED",
                    "Clear ignored — selected slot is empty",
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

class DeferredSaveClearTests(unittest.TestCase):
    def run_prepare(self, lines, sequencer, idle=True):
        collector = Mock()
        collector.snapshot.return_value = lines
        with patch.object(baseline, "_serial_sequencer_running", side_effect=sequencer), \
             patch.object(baseline, "_send_short_press") as send, \
             patch.object(baseline.time, "sleep"), \
             patch("hitl.deferred_save_idle.wait_for_deferred_save_idle_if_active", return_value=idle):
            result = baseline._prepare_deferred_save_before_clear(
                Mock(), Mock(), collector, SimpleNamespace(deferred_save_wait_ms=3000),
                press_ms=120, phase_wait_ms=500, after_index=0, abort=Mock())
            return result, send.call_count

    def test_recent_sequencer_activity_stops_without_transport_transition(self):
        result, sends = self.run_prepare([], [True, False])
        self.assertTrue(result)
        self.assertEqual(sends, 1)

    def test_active_after_stop_blocks_clear_without_retry(self):
        result, sends = self.run_prepare([], [True, True])
        self.assertFalse(result)
        self.assertEqual(sends, 1)

    def test_save_wait_timeout_blocks_clear(self):
        result, _ = self.run_prepare(["#CAP,100,SAVE,pending,1"], [False], idle=False)
        self.assertFalse(result)

    def test_recent_pending_before_fixture_baseline_is_drained_before_clear(self):
        """A save pending before this run's baseline must not reach Clear's sync drain."""
        lines = ["#CAP,100,SAVE,pending,1"]
        collector = Mock()
        collector.snapshot.return_value = lines
        with patch.object(baseline, "_serial_sequencer_running", return_value=False), \
             patch("hitl.deferred_save_idle.wait_for_deferred_save_idle_if_active", return_value=False) as wait:
            result = baseline._prepare_deferred_save_before_clear(
                Mock(), Mock(), collector, SimpleNamespace(deferred_save_wait_ms=3000),
                press_ms=120, phase_wait_ms=500, after_index=len(lines), abort=Mock())
        self.assertFalse(result)
        self.assertEqual(wait.call_count, 1)

    def test_unresolved_stuck_drain_blocks_clear(self):
        result, sends = self.run_prepare(["Persistence drain stuck after 256 iterations"], [])
        self.assertFalse(result)
        self.assertEqual(sends, 0)


if __name__ == "__main__":
    unittest.main()
