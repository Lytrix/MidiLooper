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
    _serial_has_overdubbing_stopped,
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

    def test_arm_is_not_recording_started(self) -> None:
        from hitl.legacy_record_baseline import _serial_has_recording_started

        lines = [
            "[951.469] [INFO] Loop 2: Start Recording",
            "[951.469] [INFO] [TRACK] Track 4 armed, waiting for clock to start recording",
            "#CAP,1,ST,Track,STOPPED,ARMED",
        ]
        self.assertFalse(_serial_has_recording_started(lines))
        self.assertFalse(_serial_has_recording_active(lines))
        lines = [
            "#CAP,44713251,ST,Track,STOPPED,RECORDING",
            "#CAP,44713252,RECA,0,346",
        ]
        self.assertTrue(_serial_has_recording_active(lines))
        self.assertFalse(_serial_has_armed_after(lines))

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

    def test_overdub_stop_request_is_not_completion(self) -> None:
        request_only = ["[57.931] [INFO] MIDI Button A: Stop Overdub"]
        self.assertFalse(_serial_has_overdubbing_stopped(request_only))
        completed = request_only + [
            "[57.947] [DEBUG] [TRACK] Overdubbing stopped @ tick 2704"
        ]
        self.assertTrue(_serial_has_overdubbing_stopped(completed))

    def test_stored_verification_drain_wait_requires_terminal_marker(self) -> None:
        """A partial SEVT dump must not be scored before the drain completes.

        The drain spans ~10.2 s across five phases (`sevt-fix-verify1`), so the wait
        accepts only its terminal marker and only from after the baseline.
        """
        from hitl.legacy_record_baseline import _await_stored_verification_drain

        class _Collector:
            def __init__(self, lines: list[str]) -> None:
                self._lines = list(lines)

            def snapshot(self) -> list[str]:
                return list(self._lines)

        class _Abort:
            def check(self):
                return None

        def await_drain(lines: list[str], *, after_index: int = 0) -> bool:
            return _await_stored_verification_drain(
                _Collector(lines), after_index=after_index, timeout_s=0.05, abort=_Abort()
            )

        self.assertTrue(await_drain(["#CAP,DIAG,stored_verify,drain_complete"]))
        self.assertTrue(await_drain(["#CAP,DIAG,stored_verify,drain_reset_loop"]))
        self.assertFalse(await_drain(["#CAP,11,SEVT,N,0,1,60"]))
        self.assertFalse(
            await_drain(
                [
                    "#CAP,DIAG,stored_verify,drain_complete",
                    "#CAP,11,SEVT,N,0,1,60",
                ],
                after_index=1,
            )
        )

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


class SerialVerificationRegressionTests(unittest.TestCase):
    def test_record_loop_length_accepts_bar_aligned_stop(self) -> None:
        from hitl.legacy_record_baseline import _verify_record_loop_length

        result = _verify_record_loop_length(
            [{"timestamp": 1, "raw_length": 2070, "final_length": 2304}],
            record_bars=2,
        )
        self.assertTrue(result["final_length_ok"])
        self.assertEqual(result["bar_aligned_final_length"], 2304)

    def test_persistence_handoff_accepts_work_instead_of_dispatch(self) -> None:
        from hitl.legacy_record_baseline import _summarize_persistence_handoff

        lines = [
            "#CAP,100,PERS,request,0,221184,221184,queued",
            "#CAP,200,PERS,work,10,0,0,FinalizeWorkspace,singleton,start,ok",
            "#CAP,300,PERS,result,100,221184,221184,ok",
        ]
        heap_rows = [
            {
                "timestamp": 100,
                "stage": "request",
                "duration_us": 0,
                "heap_before": 221184,
                "heap_after": 221184,
                "outcome": "queued",
            },
            {
                "timestamp": 300,
                "stage": "result",
                "duration_us": 100,
                "heap_before": 221184,
                "heap_after": 221184,
                "outcome": "ok",
            },
        ]
        summary = _summarize_persistence_handoff(heap_rows, record_stop_ts=50, lines=lines)
        self.assertIsNone(summary["first_missing_stage"])
        self.assertTrue(summary["dispatch_via_work"])


if __name__ == "__main__":
    unittest.main()
