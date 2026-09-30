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
from hitl.control_constants import TRACK_SELECT_NOTE_BASE  # noqa: E402
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

    def test_active_after_stop_and_retry_blocks_clear(self):
        """Still active after the retry stop press -> blocked, and both presses were sent.

        The retry is deliberate (see `_prepare_deferred_save_before_clear`: transport is a toggle, so a
        blind stop press can *start* it when the device already stopped itself; `captures/successor-wrap-50`
        died that way). This asserts the block still happens after the retry, which is strictly stronger
        than the pre-retry expectation of a single press, so the third sequencer probe must be supplied.
        """
        result, sends = self.run_prepare([], [True, True, True])
        self.assertFalse(result)
        self.assertEqual(sends, 2)

    def test_retry_stop_press_recovers_toggle_started_transport(self):
        """The retry's purpose: a second press clears a transport the first press accidentally started."""
        result, sends = self.run_prepare([], [True, True, False])
        self.assertTrue(result)
        self.assertEqual(sends, 2)

    def test_a_pending_save_does_not_block_the_clear(self):
        """The setup-latency contract: no deferred-save idle receipt is awaited before the clear.

        `9ca308be` removed the wait (120 s configured, floored at 240 s): on device it aborted setup
        when the boot restore was slow and never made the clear more likely to succeed
        (`captures/s4f22-base-device-16`). After boot the device is driven straight away and the
        firmware owns the refusal, which is read downstream as a clear abort.
        """
        result, _ = self.run_prepare(["#CAP,100,SAVE,pending,1"], [False], idle=False)
        self.assertTrue(result)

    def test_a_save_pending_before_the_baseline_is_reported_not_waited_on(self):
        """`deferred_save_active_in_tail` still *sees* the earlier pending save; it just does not wait.

        The recent-window anchor stays (a pending writer must not be missed), and no
        `wait_for_deferred_save_idle_if_active` call may be issued for it.
        """
        lines = ["#CAP,100,SAVE,pending,1"]
        collector = Mock()
        collector.snapshot.return_value = lines
        with patch.object(baseline, "_serial_sequencer_running", return_value=False), \
             patch("hitl.deferred_save_idle.wait_for_deferred_save_idle_if_active") as wait:
            result = baseline._prepare_deferred_save_before_clear(
                Mock(), Mock(), collector, SimpleNamespace(deferred_save_wait_ms=3000),
                press_ms=120, phase_wait_ms=500, after_index=len(lines), abort=Mock())
        self.assertTrue(result)
        self.assertEqual(wait.call_count, 0)

    def test_unresolved_stuck_drain_blocks_clear(self):
        result, sends = self.run_prepare(["Persistence drain stuck after 256 iterations"], [])
        self.assertFalse(result)
        self.assertEqual(sends, 0)


class ClearRefusalTests(unittest.TestCase):
    """The firmware's **temporary** clear refusal is retryable; its abort is not.

    `MidiButtonActions.cpp` prints two different rows for the deferred-save outcomes of a clear.
    `post_load_holdoff` is the fresh-boot case (`captures/s4f22-step11-h4`: the press was refused ~17 s
    after the boot, the slot was left untouched and the save was still owed, and the run then died with
    `clear_not_confirmed` because the transition only retries a *disproved* request).
    """

    def setUp(self) -> None:
        from hitl import legacy_record_baseline as baseline

        self.baseline = baseline

    def test_temporary_refusal_reason_is_read(self) -> None:
        lines = [
            "[16.764] [INFO] Clear waiting for deferred save completion",
            "[StorageManager] ERROR: Persistence drain blocked by post_load_holdoff",
            "[16.799] [ERROR] Clear refused: deferred save temporarily blocked by "
            "post_load_holdoff; slot left untouched, save still owed",
        ]
        self.assertEqual(
            "post_load_holdoff", self.baseline._serial_clear_refusal(lines)
        )

    def test_terminal_abort_is_not_a_refusal(self) -> None:
        lines = ["[1.2] [ERROR] Clear aborted: could not complete deferred save first (drain_stuck)"]
        self.assertIsNone(self.baseline._serial_clear_refusal(lines))
        self.assertTrue(self.baseline._serial_has_clear_aborted(lines))

    def test_the_newest_row_wins(self) -> None:
        lines = [
            "Clear refused: deferred save temporarily blocked by post_load_holdoff; slot left untouched",
            "MIDI: Clear selected slot 1",
        ]
        # A later completed clear supersedes the refusal in the same window only when the caller
        # scopes the scan; the predicate itself reports the newest refusal row it sees.
        self.assertEqual("post_load_holdoff", self.baseline._serial_clear_refusal(lines))
        self.assertIsNone(
            self.baseline._serial_clear_refusal(lines, after_index=1)
        )

    def test_retry_budget_is_bounded(self) -> None:
        self.assertGreaterEqual(self.baseline.CLEAR_REFUSAL_RETRIES, 1)
        self.assertLessEqual(self.baseline.CLEAR_REFUSAL_RETRIES, 3)
        self.assertGreater(self.baseline.CLEAR_REFUSAL_SAVE_WAIT_S, 0.0)


class ClearBeforeTransportTests(unittest.TestCase):
    """The pre-transport clear pass (`_clear_tracks_before_transport`).

    Requirement (2026-09-30): every (track, slot) the run will touch is cleared **before** the
    transport start press, and the per-track path then skips a clear it already did. This pins the
    pass's own contract -- one clear per track, the verdict rows it contributes, and the named
    failure that stops the run before any transport press.
    """

    @staticmethod
    def _args(**overrides):
        kwargs = dict(
            first_track_index=0,
            track_count=2,
            press_ms=120,
            phase_wait_ms=5,
            no_loop_edit_precondition=True,
            loop_slot=0,
        )
        kwargs.update(overrides)
        return SimpleNamespace(**kwargs)

    def test_every_track_is_selected_and_cleared_once(self) -> None:
        collector = Mock()
        collector.snapshot.return_value = []
        results: list[dict[str, object]] = []
        failures: list[dict[str, object]] = []
        presses: list[int] = []
        with patch.object(
            baseline, "_send_short_press",
            side_effect=lambda _port, **kwargs: presses.append(kwargs["note"]),
        ), patch.object(
            baseline, "_run_clear_before_record", return_value=(True, "clear_log_confirmed")
        ) as clear, patch.object(baseline.time, "sleep"):
            ok, cleared = baseline._clear_tracks_before_transport(
                Mock(), Mock(), collector, self._args(),
                abort=Mock(check=Mock(return_value=None)),
                clear_results=results,
                precondition_failures=failures,
            )
        self.assertTrue(ok)
        self.assertEqual(set(cleared), {0, 1})
        # One track select per track, and each track is cleared before the transport is started.
        self.assertEqual(presses, [TRACK_SELECT_NOTE_BASE, TRACK_SELECT_NOTE_BASE + 1])
        self.assertEqual(clear.call_count, 2)
        self.assertEqual(
            results,
            [
                {"track_index": 0, "result": "clear_log_confirmed"},
                {"track_index": 1, "result": "clear_log_confirmed"},
            ],
        )
        self.assertEqual(failures, [])

    def test_a_failed_clear_stops_the_pass_with_the_verdict_row(self) -> None:
        collector = Mock()
        collector.snapshot.return_value = []
        results: list[dict[str, object]] = []
        failures: list[dict[str, object]] = []
        with patch.object(baseline.time, "sleep"), patch.object(
            baseline, "_send_short_press"
        ), patch.object(
            baseline, "_run_clear_before_record",
            side_effect=[(True, "empty_transition"), (False, "clear_not_confirmed")],
        ):
            ok, cleared = baseline._clear_tracks_before_transport(
                Mock(), Mock(), collector, self._args(),
                abort=Mock(check=Mock(return_value=None)),
                clear_results=results,
                precondition_failures=failures,
            )
        self.assertFalse(ok)
        # Only the track that cleared is recorded, and it is the one the per-track path may skip.
        self.assertEqual(set(cleared), {0})
        self.assertEqual(results, [{"track_index": 0, "result": "empty_transition"}])
        self.assertEqual(
            failures,
            [
                {
                    "track_index": 1,
                    "step": "clear_to_empty",
                    "reason": "clear_not_confirmed",
                }
            ],
        )

    def test_an_abort_stops_the_pass_without_touching_the_next_track(self) -> None:
        collector = Mock()
        collector.snapshot.return_value = []
        with patch.object(baseline.time, "sleep"), patch.object(
            baseline, "_send_short_press"
        ), patch.object(baseline, "_run_clear_before_record") as clear:
            ok, cleared = baseline._clear_tracks_before_transport(
                Mock(), Mock(), collector, self._args(),
                abort=Mock(check=Mock(return_value="run deadline")),
                clear_results=[],
                precondition_failures=[],
            )
        self.assertFalse(ok)
        self.assertEqual(cleared, {})
        self.assertEqual(clear.call_count, 0)


class LoopEditPreconditionTests(unittest.TestCase):
    """The device boots into LOOP_EDIT, but a run must *establish* it, not assume it.

    The record/overdub gestures mean something else outside LOOP_EDIT, and this run's own
    pre-transport clear (row selects plus a long press) can leave the mode, so an unproven edit mode
    must not be accepted silently: it is either already proven by the capture's own ack, or proved
    by holding the edit button long (its exit action) and reading the ack that press leaves.
    """

    _LOOP_ACK = "Edit session: LOOP_EDIT (Program 0, Note 100 trigger)"
    _NOTE_ACK = "Edit session: NOTE_EDIT (Program 1, Note 0 trigger)"
    _EXIT_ACK = "[INFO] MIDI Encoder: Long press - exited edit mode"

    class _Collector:
        def __init__(self, lines: list[str]) -> None:
            self._lines = lines

        def snapshot(self) -> list[str]:
            return list(self._lines)

    class _Port:
        def __init__(self) -> None:
            self.sent: list = []

        def send(self, message) -> None:
            self.sent.append(message)

    def _precondition(self, lines: list[str], *, confirm: str):
        from hitl.edit_mode_precondition import ensure_loop_edit_before_record

        collector = self._Collector(lines)
        port = self._Port()
        original_snapshot = collector.snapshot
        state = {"sent": False}

        def snapshot() -> list[str]:
            if state["sent"] and confirm:
                return original_snapshot() + [confirm]
            return original_snapshot()

        def _send(message) -> None:
            state["sent"] = True
            port.sent.append(message)

        collector.snapshot = snapshot  # type: ignore[method-assign]
        port.send = _send  # type: ignore[method-assign]
        with patch("hitl.edit_mode_precondition.time.sleep"):
            ok = ensure_loop_edit_before_record(
                port, collector, phase_wait_ms=100, timeout_s=0.2,
            )
        return ok, port

    def test_unproven_mode_is_entered_explicitly_and_proven(self) -> None:
        ok, port = self._precondition(
            ["boot: no edit session yet"], confirm=self._LOOP_ACK,
        )
        self.assertTrue(ok)
        # One long press of the edit button (note 38) is what lands the device in LOOP_EDIT.
        presses = [m.note for m in port.sent if m.type == "note_on"]
        self.assertEqual(presses, [38])
        self.assertTrue(all(m.channel == 15 for m in port.sent))

    def test_unproven_mode_without_an_ack_is_refused(self) -> None:
        ok, port = self._precondition(["boot: no edit session yet"], confirm="")
        self.assertFalse(ok)
        # It tried to establish LOOP_EDIT before refusing.
        self.assertEqual([m.note for m in port.sent if m.type == "note_on"], [38])

    def test_already_in_loop_edit_needs_no_gesture(self) -> None:
        ok, port = self._precondition([self._LOOP_ACK], confirm="")
        self.assertTrue(ok)
        self.assertEqual(port.sent, [])

    def test_note_edit_is_left_by_the_long_press(self) -> None:
        ok, port = self._precondition([self._NOTE_ACK], confirm=self._EXIT_ACK)
        self.assertTrue(ok)
        # The exit is a *long* press, not a short one: a short press would cycle the mode again.
        self.assertEqual([m.note for m in port.sent if m.type == "note_on"], [38])


if __name__ == "__main__":
    unittest.main()
