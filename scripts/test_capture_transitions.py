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
    _effective_transition_actual,
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
    _await_collector_marker,
    _marker_micros,
    _probe_press_delays,
    _can_skip_clear_before_record,
    _evaluate_clear_precondition,
    _serial_has_clear_aborted,
    _track_cleared_for_record,
    _latest_device_bar_index,
    _verify_record_loop_length,
    _verify_successor_reentry,
    _verify_undo_redo_display,
)
from hitl.serial_timing import extract_phase_sessions
from hitl.serial_collector import serial_line_resets_heartbeat



class DeferredOverdubStopOracleTests(unittest.TestCase):
    """The deferred stop is OVERDUBBING -> OVERDUB_STOPPING -> PLAYING.

    The oracle used to require the direct OVERDUBBING -> PLAYING edge, so every
    deferred stop looked like a missing phase boundary and its first-note window
    never closed.
    """

    def test_deferred_hop_counts_as_the_playing_transition(self) -> None:
        deferred = {("OVERDUBBING", "OVERDUB_STOPPING"): 1, ("OVERDUB_STOPPING", "PLAYING"): 1}
        self.assertEqual(
            1, _effective_transition_actual("OVERDUBBING", "PLAYING", deferred)
        )
        # Immediate stops keep counting, and both shapes sum when both occurred.
        self.assertEqual(
            1, _effective_transition_actual("OVERDUBBING", "PLAYING", {("OVERDUBBING", "PLAYING"): 1})
        )
        both = dict(deferred)
        both[("OVERDUBBING", "PLAYING")] = 1
        self.assertEqual(2, _effective_transition_actual("OVERDUBBING", "PLAYING", both))

    def test_overdub_phase_session_closes_on_the_deferred_stop(self) -> None:
        lines = [
            "#CAP,1000000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,2000000,MI,U,144,4,60,98",
            "#CAP,3000000,ST,Track,OVERDUBBING,OVERDUB_STOPPING",
            "#CAP,3000020,ST,Track,OVERDUB_STOPPING,PLAYING",
        ]
        report = extract_phase_sessions(lines)
        self.assertIsNotNone(report.overdub)
        assert report.overdub is not None
        self.assertEqual(1000000, report.overdub.start_ts)
        self.assertEqual(3000000, report.overdub.stop_ts)
        self.assertIsNone(report.overdub.ambiguity)
        self.assertFalse(
            any("without stop" in diagnostic for diagnostic in report.diagnostics)
        )

    def test_immediate_stop_still_pairs(self) -> None:
        lines = [
            "#CAP,1000000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,3000000,ST,Track,OVERDUBBING,PLAYING",
        ]
        report = extract_phase_sessions(lines)
        assert report.overdub is not None
        self.assertEqual((1000000, 3000000), (report.overdub.start_ts, report.overdub.stop_ts))


class ProxyTimedRecordLengthOracleTests(unittest.TestCase):
    """A wall-clock-proxied stop cannot predict the device's bar grid.

    The recorded length is then a report, not a firmware verdict: any bar-aligned
    length at or above the request is consistent with the proxy and must not be
    raised as a content defect.
    """

    def test_proxy_timed_longer_bar_aligned_length_is_reported_not_enforced(self) -> None:
        rows = [{"final_length": 7680, "raw_length": 7722}]
        proxied = _verify_record_loop_length(rows, record_bars=8, proxy_timed=True)
        self.assertTrue(proxied["final_length_ok"])
        self.assertTrue(proxied["proxy_only_slop"])
        self.assertEqual(7680, proxied["actual_final_length"])
        self.assertEqual(6144, proxied["expected_final_length"])

        enforced = _verify_record_loop_length(rows, record_bars=8, proxy_timed=False)
        self.assertFalse(enforced["final_length_ok"])
        self.assertFalse(enforced["proxy_only_slop"])

    def test_proxy_timed_does_not_accept_a_non_bar_aligned_length(self) -> None:
        rows = [{"final_length": 7000, "raw_length": 7000}]
        result = _verify_record_loop_length(rows, record_bars=8, proxy_timed=True)
        self.assertFalse(result["final_length_ok"])



class DeviceBarProbeOracleTests(unittest.TestCase):
    """The device's own BAR index is the authority for a phase's musical length.

    Host clock-message counting and seconds-per-bar estimates both over-ran on device
    (11-34 bars for an 8-bar request), while the device quantizes the loop from its own
    bar counter.
    """

    def test_latest_index_reads_the_newest_bar_row(self) -> None:
        lines = [
            "#CAP,100,BAR,0,0",
            "#CAP,200,MO,144,4,60,98",
            "#CAP,300,BAR,768,1",
            "#CAP,400,BAR,1536,2",
            "#CAP,500,RING,overflow,1",
        ]
        self.assertEqual(2, _latest_device_bar_index(lines))

    def test_no_bar_rows_is_none(self) -> None:
        self.assertIsNone(_latest_device_bar_index([]))
        self.assertIsNone(_latest_device_bar_index(["#CAP,1,MO,144,4,60,98"]))

    def test_malformed_index_is_skipped(self) -> None:
        lines = ["#CAP,100,BAR,768,1", "#CAP,200,BAR,1536,notanindex"]
        self.assertEqual(1, _latest_device_bar_index(lines))

class SuccessorReentryProbeOracleTests(unittest.TestCase):
    """The probe proves the two halves of the DEC-067 contract.

    A successor START is admitted while the predecessor's deferred job is pending
    (before the `Reentry released` marker), and its own STOP is refused until the
    predecessor's finalization resources are admitted again. Device times are micros in
    both the `#CAP` rows and the human `[<seconds>.<ms>]` prefixes.
    """

    _OVERLAPPING = [
        "#CAP,1000000,ST,Track,PLAYING,OVERDUBBING",
        "#CAP,2000000,ST,Track,OVERDUBBING,OVERDUB_STOPPING",
        "#CAP,2001000,ST,Track,OVERDUB_STOPPING,PLAYING",
        "[2.100] [INFO] [TRACK] Reentry window open slot=0",
        "#CAP,2300000,ST,Track,PLAYING,OVERDUBBING",
        "[2.300] [DEBUG] [TRACK] Overdubbing started @ tick 100",
        "[2.600] [INFO] [TRACK] Overdub stop deferred: deferred/frozen admission role is occupied",
        "#CAP,3000000,ODUB,stop,bridge_ready,0,0,0,0,1,7680,context_retained",
        "[3.400] [INFO] [TRACK] Reentry released",
        "#CAP,3500000,ST,Track,OVERDUBBING,OVERDUB_STOPPING",
    ]

    def test_overlapping_successor_with_denied_stop(self) -> None:
        result = _verify_successor_reentry(
            self._OVERLAPPING, requested_delay_ms=500, requested_stop_delay_ms=300
        )
        self.assertTrue(result["probe_requested"])
        self.assertTrue(result["overlap_ok"])
        self.assertEqual(2_100_000, result["reentry_window_ts"])
        self.assertEqual(3_400_000, result["finalization_released_ts"])
        # The admitted successor's own stop lands after the release: the quick press in
        # between produced no stop edge, which is the denial half.
        self.assertTrue(result["denied_stop_ok"])
        self.assertEqual(2_600_000, result["denied_stop_ts"])
        self.assertEqual(300.0, result["successor_start_delay_ms"])
        self.assertEqual(1100.0, result["successor_start_before_release_ms"])

    def test_stop_edge_before_the_release_is_a_denial_failure(self) -> None:
        lines = [
            "#CAP,1000000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,2000000,ST,Track,OVERDUBBING,OVERDUB_STOPPING",
            "[2.100] [INFO] [TRACK] Reentry window open slot=0",
            "#CAP,2300000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,2400000,ST,Track,OVERDUBBING,OVERDUB_STOPPING",
            "[3.400] [INFO] [TRACK] Reentry released",
        ]
        result = _verify_successor_reentry(
            lines, requested_delay_ms=500, requested_stop_delay_ms=300
        )
        self.assertTrue(result["overlap_ok"])
        self.assertFalse(result["denied_stop_ok"])

    def test_current_firmware_denial_wording_counts_as_the_denial(self) -> None:
        # DEC-070 and later refuse through `Track::stopOverdubbing`'s reason list
        # (`Overdub stop refused: stop_job_pending ...`) instead of the DEC-067
        # frozen-role string. Both are the same contract; accepting only the old one made
        # every current-firmware probe report `successor_reentry_stop_not_denied`
        # (`captures/successor-ownership-03`).
        lines = [
            "#CAP,1000000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,2000000,ST,Track,OVERDUBBING,OVERDUB_STOPPING",
            "[2.100] [INFO] [TRACK] Reentry window open slot=0",
            "#CAP,2300000,ST,Track,PLAYING,OVERDUBBING",
            "[2.300] [DEBUG] [TRACK] Overdubbing started @ tick 100",
            "[2.600] [INFO] [TRACK] Overdub stop refused: stop_job_pending phase=9 dest=0 "
            "closure=0 recycle_owner=0 recycle_started=0 undo_ready=1",
            "[3.400] [INFO] [TRACK] Reentry released",
            "[3.900] [INFO] [TRACK] Reentry window open slot=0",
            "[3.920] [INFO] [TRACK] Reentry released",
        ]
        result = _verify_successor_reentry(
            lines, requested_delay_ms=500, requested_stop_delay_ms=300
        )
        self.assertTrue(result["overlap_ok"])
        self.assertEqual(2_600_000, result["denied_stop_ts"])
        self.assertTrue(result["denied_stop_ok"])
        self.assertEqual(3_900_000, result["admitted_stop_ts"])
        self.assertTrue(result["retirement_ok"])

    def test_sequential_successor_is_not_overlapping(self) -> None:
        lines = [
            "#CAP,1000000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,2000000,ST,Track,OVERDUBBING,OVERDUB_STOPPING",
            "[2.100] [INFO] [TRACK] Reentry window open slot=0",
            "[2.900] [INFO] [TRACK] Reentry released",
            "#CAP,9000000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,10000000,ST,Track,OVERDUBBING,OVERDUB_STOPPING",
        ]
        result = _verify_successor_reentry(
            lines, requested_delay_ms=500, requested_stop_delay_ms=0
        )
        self.assertFalse(result["overlap_ok"])

    def test_supersession_without_a_release_marker_still_proves_overlap(self) -> None:
        # Supersession can retire the predecessor without a bridge_ready or release row;
        # the window marker plus the successor's start is then the available evidence.
        lines = [
            "#CAP,1000000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,2000000,ST,Track,OVERDUBBING,OVERDUB_STOPPING",
            "[2.100] [INFO] [TRACK] Reentry window open slot=0",
            "#CAP,2300000,ST,Track,PLAYING,OVERDUBBING",
        ]
        result = _verify_successor_reentry(
            lines, requested_delay_ms=500, requested_stop_delay_ms=0
        )
        self.assertTrue(result["overlap_ok"])
        self.assertIsNone(result["finalization_released_ts"])

    def test_probe_off_reports_no_verdict(self) -> None:
        result = _verify_successor_reentry(
            self._OVERLAPPING, requested_delay_ms=0, requested_stop_delay_ms=0
        )
        self.assertFalse(result["probe_requested"])
        self.assertIsNone(result["overlap_ok"])


class ReentryMarkerTests(unittest.TestCase):
    def test_human_marker_micros_uses_the_millis_prefix(self) -> None:
        self.assertEqual(2_100_000, _marker_micros(
            ["[2.100] [INFO] [TRACK] Reentry window open slot=0"], "Reentry window open"))
        self.assertIsNone(_marker_micros(["[2.100] [INFO] other"], "Reentry window open"))

    def test_marker_micros_reads_a_capture_row_when_present(self) -> None:
        lines = ["#CAP,3400000,DIAG,canonical_release,0,1536,8"]
        self.assertEqual(3_400_000, _marker_micros(lines, "DIAG,canonical_release,"))

    def test_await_marker_scans_without_copying_the_snapshot(self) -> None:
        class Collector:
            def __init__(self, lines):
                self.lines = lines
                self.scans = 0

            def find_line(self, needle, *, after_index=0):
                self.scans += 1
                for index in range(after_index, len(self.lines)):
                    if needle in self.lines[index]:
                        return index
                return None

            def snapshot(self):  # pragma: no cover - must not be used by the waiter
                raise AssertionError("marker wait must not copy the snapshot")

        collector = Collector(["[1.000] [INFO] [TRACK] Reentry released"])
        seen = _await_collector_marker(collector, "Reentry released", baseline=0,
                                       timeout_s=0.2, poll_s=0.0)
        self.assertIsNotNone(seen)
        self.assertEqual(1, collector.scans)
        missing = _await_collector_marker(collector, "Reentry window open", baseline=1,
                                          timeout_s=0.01, poll_s=0.0)
        self.assertIsNone(missing)

    def test_await_marker_falls_back_to_a_snapshot_walk(self) -> None:
        # The managed-capture follow collector had no `find_line` at first; a silent miss sent
        # the probe down its degraded path (successor-reentry-21).
        class SnapshotOnlyCollector:
            def snapshot(self):
                return ["[1.000] [INFO] [TRACK] Reentry window open slot=0"]

        seen = _await_collector_marker(SnapshotOnlyCollector(), "Reentry window open",
                                       baseline=0, timeout_s=0.2, poll_s=0.0)
        self.assertIsNotNone(seen)


class SuccessorProbePressScheduleTests(unittest.TestCase):
    """The probe's two presses are scheduled in their own process (hitl.probe_press).

    `successor-reentry-18` measured +1535 ms against a requested +400 ms because the deferred
    stop's serial flood starves the harness process, so the presses get delays measured from a
    go-ahead line instead. These cases pin the device's double-press floor.
    """

    def test_successor_delay_respects_the_double_press_floor(self) -> None:
        successor_ms, denial_ms = _probe_press_delays(
            probe_delay_ms=400, press_ms=120, stop_delay_ms=400
        )
        # press_ms + 380 = 500 ms is the floor: below it the device reads a double press.
        self.assertEqual(500, successor_ms)
        self.assertEqual(400, denial_ms)

    def test_requested_delay_wins_when_it_clears_the_floor(self) -> None:
        successor_ms, denial_ms = _probe_press_delays(
            probe_delay_ms=900, press_ms=120, stop_delay_ms=0
        )
        self.assertEqual(900, successor_ms)
        self.assertEqual(0, denial_ms)

    def test_negative_stop_delay_is_no_denial_press(self) -> None:
        _, denial_ms = _probe_press_delays(
            probe_delay_ms=500, press_ms=120, stop_delay_ms=-5
        )
        self.assertEqual(0, denial_ms)


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


class UndoRedoDisplayOracleTests(unittest.TestCase):
    """A global undo/redo of a record/overdub entry must repaint the committed frame.

    `content-verify-65` looked green on the stop-oracle while the OLED kept the undone overdub:
    the frame stayed 193 against a record-only committed 180 across both `Undo` and `Redo`.
    """

    @staticmethod
    def _lines(before: int, after_undo: int, after_redo: int) -> list[str]:
        return [
            "#CAP,71000000,DISP,0,PLAYING,6912,194,180,"
            f"{before},{before},1\r\n",
            "[73.582] [DEBUG] Undo applied: kind=1 undo_count=1",
            "#CAP,73637013,DISP,0,PLAYING,6912,193,180,"
            f"{after_undo},{after_undo},1\r\n",
            "[76.775] [DEBUG] Redo applied: kind=1 redo_count=0",
            "#CAP,77459037,DISP,0,STOPPED,6912,193,193,"
            f"{after_redo},{after_redo},1\r\n",
        ]

    def test_frame_that_keeps_the_undone_overdub_is_flagged(self) -> None:
        result = _verify_undo_redo_display(self._lines(194, 193, 193))
        self.assertEqual(194, result["frame_before_undo"])
        self.assertEqual(193, result["frame_after_undo"])
        self.assertEqual(193, result["frame_after_redo"])
        self.assertIn("display_redo_did_not_restore_the_overdub", result["issues"])

    def test_frame_that_drops_then_restores_the_overdub_passes(self) -> None:
        result = _verify_undo_redo_display(self._lines(194, 180, 193))
        self.assertEqual([], result["issues"])

    def test_undo_that_changes_nothing_is_flagged(self) -> None:
        result = _verify_undo_redo_display(self._lines(193, 193, 193))
        self.assertIn("display_undo_kept_the_overdub", result["issues"])
        self.assertIn("display_redo_did_not_restore_the_overdub", result["issues"])

    def test_missing_markers_are_not_evaluated(self) -> None:
        lines = [
            "#CAP,73637013,DISP,0,PLAYING,6912,193,180,193,193,1\r\n",
        ]
        result = _verify_undo_redo_display(lines)
        self.assertIsNone(result["undo_ts"])
        self.assertEqual([], result["issues"])

    def test_pre_undo_ramp_uses_the_steady_peak(self) -> None:
        # Several sessions: the frame is still ramping when the undo lands. The last row
        # understates the content the undo must remove (`gus-undo-04`: 144 last vs 176 steady).
        lines = [
            "#CAP,70000000,DISP,0,PLAYING,6912,144,120,144,144,1\r\n",
            "#CAP,71000000,DISP,0,PLAYING,6912,176,176,176,176,1\r\n",
            "[77.844] [DEBUG] Undo applied: kind=1 undo_count=1",
            "#CAP,78000000,DISP,0,PLAYING,6912,160,160,160,160,1\r\n",
            "[81.062] [DEBUG] Redo applied: kind=1 redo_count=0",
            "#CAP,82000000,DISP,0,PLAYING,6912,176,176,176,176,1\r\n",
        ]
        result = _verify_undo_redo_display(lines)
        self.assertEqual(176, result["frame_before_undo"])
        self.assertEqual([], result["issues"])

    def test_multiple_cycles_bound_the_post_redo_sample(self) -> None:
        # `undo_redo_cycles > 1`: the next undo lands inside the 8 s window, and sampling it as the
        # post-redo frame reports a false redo failure (`gus-undo-06` ran three cycles).
        lines = [
            "#CAP,70000000,DISP,0,PLAYING,6912,176,176,176,176,1\r\n",
            "[77.949] [DEBUG] Undo applied: kind=1 undo_count=1",
            "#CAP,78000000,DISP,0,PLAYING,6912,144,144,144,144,1\r\n",
            "[81.094] [DEBUG] Redo applied: kind=1 redo_count=0",
            "#CAP,82000000,DISP,0,PLAYING,6912,176,176,176,176,1\r\n",
            "[87.666] [DEBUG] Undo applied: kind=1 undo_count=1",
            "#CAP,88000000,DISP,0,PLAYING,6912,144,144,144,144,1\r\n",
            "[91.081] [DEBUG] Redo applied: kind=1 redo_count=0",
            "#CAP,92000000,DISP,0,PLAYING,6912,176,176,176,176,1\r\n",
        ]
        result = _verify_undo_redo_display(lines)
        self.assertEqual(176, result["frame_after_redo"])
        self.assertEqual([], result["issues"])


if __name__ == "__main__":
    unittest.main()
