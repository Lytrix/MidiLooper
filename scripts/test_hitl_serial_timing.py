#!/usr/bin/env python3
"""Unit tests for HITL serial timing verification helpers."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.serial_timing import (
    extract_first_note_offset,
    extract_phase_boundaries,
    extract_phase_sessions,
    phase_start_delay_clocks,
)
from hitl.contract import PhaseAmbiguity
from hitl.verify.midi_boundary_timing import (
    extract_overdub_stopping_segments,
    extract_deadline_samples,
    extract_stop_phase_samples,
    extract_stop_timeline,
    midi_boundary_timing_issues,
    summarize_midi_boundary_timing,
)
from hitl.managed_capture import wait_for_capture_boot_ready


class ExtractPhaseBoundariesTests(unittest.TestCase):
    def test_record_and_overdub_windows(self) -> None:
        lines = [
            "#CAP,1000,ST,Track,ARMED,RECORDING",
            "#CAP,2000,ST,Track,RECORDING,STOPPED_RECORDING",
            "#CAP,3000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,4000,ST,Track,OVERDUBBING,PLAYING",
        ]
        bounds = extract_phase_boundaries(lines)
        self.assertEqual(bounds["record_start_ts"], 1000)
        self.assertEqual(bounds["record_stop_ts"], 2000)
        self.assertEqual(bounds["overdub_start_ts"], 3000)
        self.assertEqual(bounds["overdub_stop_ts"], 4000)

    def test_overdub_windows_from_human_logs_when_st_missing(self) -> None:
        lines = [
            "#CAP,1000,ST,Track,ARMED,RECORDING",
            "#CAP,2000,ST,Track,RECORDING,STOPPED_RECORDING",
            "[1841.970] [DEBUG] [TRACK] Overdubbing started @ tick 702",
            "[1847.139] [DEBUG] [TRACK] Overdubbing stopped @ tick 2677",
        ]
        bounds = extract_phase_boundaries(lines)
        self.assertEqual(bounds["overdub_start_ts"], 1841970000)
        self.assertEqual(bounds["overdub_stop_ts"], 1847139000)

    def test_record_boundaries_from_human_wall_clock(self) -> None:
        lines = [
            "[1836.133] [DEBUG] [TRACK] Recording started @ tick 0",
            "[1840.889] [INFO] Record stop truncation rewind: raw=1827 final=1536 playbackTick=291",
            "[1840.890] [DEBUG] [TRACK] Recording stopped @ tick 291 (recStart=0 length=1536)",
        ]
        bounds = extract_phase_boundaries(lines)
        self.assertEqual(bounds["record_start_ts"], 1836133000)
        self.assertEqual(bounds["record_stop_ts"], 1840890000)

    def test_uses_last_complete_recording_window(self) -> None:
        lines = [
            "#CAP,400,ST,Track,EMPTY,RECORDING",
            "#CAP,500,ST,Track,RECORDING,STOPPED_RECORDING",
            "#CAP,1000,ST,Track,ARMED,RECORDING",
            "#CAP,2000,ST,Track,RECORDING,STOPPED_RECORDING",
            "#CAP,3000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,4000,ST,Track,OVERDUBBING,PLAYING",
        ]
        bounds = extract_phase_boundaries(lines)
        self.assertEqual(bounds["record_start_ts"], 1000)
        self.assertEqual(bounds["record_stop_ts"], 2000)

    def test_second_overdub_stop_from_odub_enter_when_st_missing(self) -> None:
        # OD1 closed by ST. OD2 opened by ST and sealed without OVERDUBBING→PLAYING.
        lines = [
            "#CAP,1000,ST,Track,ARMED,RECORDING",
            "#CAP,2000,ST,Track,RECORDING,STOPPED_RECORDING",
            "#CAP,3000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,4000,ST,Track,OVERDUBBING,PLAYING",
            "#CAP,5000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,8000,ODUB,stop,enter,37,0,4294967295,4294967295,78,5,entered",
            "[0.008] [DEBUG] [TRACK] Overdubbing stopped @ tick 12397",
            "#CAP,8100,MI,U,144,5,48,100",
        ]
        bounds = extract_phase_boundaries(lines)
        self.assertEqual(bounds["overdub_start_ts"], 3000)
        self.assertEqual(bounds["overdub_stop_ts"], 4000)
        self.assertEqual(bounds["second_overdub_start_ts"], 5000)
        self.assertEqual(bounds["second_overdub_stop_ts"], 8000)

    def test_first_overdub_stop_from_odub_enter_keeps_time_order(self) -> None:
        # [`144036`]: OD1 sealed without OVERDUBBING→PLAYING; OD2 has ST stop.
        lines = [
            "#CAP,81814170,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,90544026,ODUB,stop,enter,26,0,4294967295,4294967295,36,2,entered",
            "[90.546] [DEBUG] [TRACK] Overdubbing stopped @ tick 6469",
            "#CAP,91173391,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,105954997,ODUB,stop,enter,38,0,4294967295,4294967295,78,5,entered",
            "#CAP,105958326,ST,Track,OVERDUBBING,PLAYING",
            "[105.958] [DEBUG] [TRACK] Overdubbing stopped @ tick 12378",
        ]
        bounds = extract_phase_boundaries(lines)
        self.assertEqual(bounds["overdub_start_ts"], 81814170)
        self.assertEqual(bounds["overdub_stop_ts"], 90544026)
        self.assertEqual(bounds["second_overdub_start_ts"], 91173391)
        self.assertEqual(bounds["second_overdub_stop_ts"], 105954997)

    def test_stale_pre_baseline_sessions_are_ignored(self) -> None:
        lines = [
            "#CAP,100,ST,Track,ARMED,RECORDING",
            "#CAP,200,ST,Track,RECORDING,STOPPED_RECORDING",
            "#CAP,300,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,400,ST,Track,OVERDUBBING,PLAYING",
            "baseline",
            "#CAP,1000,ST,Track,ARMED,RECORDING",
            "#CAP,2000,ST,Track,RECORDING,STOPPED_RECORDING",
            "#CAP,3000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,4000,ST,Track,OVERDUBBING,PLAYING",
        ]
        bounds = extract_phase_boundaries(lines, after_index=4)
        self.assertEqual(bounds["record_start_ts"], 1000)
        self.assertEqual(bounds["overdub_start_ts"], 3000)
        self.assertIsNone(extract_phase_boundaries(lines[:5], after_index=4)["record_start_ts"])

    def test_hitl_header_scopes_phase_sessions(self) -> None:
        lines = [
            "#CAP,100,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,200,ST,Track,OVERDUBBING,PLAYING",
            "#HITL,SCENARIO,record_overdub",
            "#CAP,1000,ST,Track,ARMED,RECORDING",
            "#CAP,2000,ST,Track,RECORDING,STOPPED_RECORDING",
            "#CAP,3000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,4000,ST,Track,OVERDUBBING,PLAYING",
        ]
        bounds = extract_phase_boundaries(lines)
        self.assertEqual(bounds["record_start_ts"], 1000)
        self.assertEqual(bounds["overdub_start_ts"], 3000)

    def test_unmatched_overdub_start_is_missing_stop(self) -> None:
        lines = [
            "#CAP,1000,ST,Track,ARMED,RECORDING",
            "#CAP,2000,ST,Track,RECORDING,STOPPED_RECORDING",
            "#CAP,3000,ST,Track,PLAYING,OVERDUBBING",
        ]
        report = extract_phase_sessions(lines)
        self.assertEqual(report.overdub.start_ts if report.overdub else None, 3000)
        self.assertIsNone(report.overdub.stop_ts if report.overdub else 0)
        self.assertIn(PhaseAmbiguity.MISSING_STOP, report.ambiguities)

    def test_record_take_holding_the_stream_wins_when_the_stray_is_first(self) -> None:
        # Today's device shape (`captures/session_20260930_132517.log`): a transient one-bar take
        # first (768 ticks, no host notes), the intended 2-bar take last (1536 = the stream). The
        # old "first pair" rule scored the empty take and reported `record on/off 0/0`.
        lines = [
            "#CAP,1000,ST,Track,ARMED,RECORDING",
            "#CAP,1600,ST,Track,RECORDING,STOPPED_RECORDING",  # stray take, no notes
            "#CAP,9000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,9500,ST,Track,OVERDUBBING,PLAYING",
            "#CAP,30000,ST,Track,ARMED,RECORDING",
            "#CAP,34000,ST,Track,RECORDING,STOPPED_RECORDING",  # the intended take
            "#CAP,30500,MI,U,144,5,60,100",
            "#CAP,31500,MI,U,144,5,62,100",
            "#CAP,32500,MI,U,144,5,64,100",
        ]
        report = extract_phase_sessions(lines)
        self.assertEqual(report.record.start_ts, 30000)
        self.assertEqual(report.record.stop_ts, 34000)
        self.assertTrue(any("record take 2 of 2 chosen" in d for d in report.diagnostics))

    def test_record_take_holding_the_stream_wins_when_the_stray_is_last(self) -> None:
        # The 2026-09-29 shape (`captures/s4f22-step8-e2`): intended take first (2304 ticks), stray
        # one-bar re-arm last. Selecting by note membership must keep the first take here, so the
        # fix does not regress that lesson.
        lines = [
            "#CAP,1000,ST,Track,ARMED,RECORDING",
            "#CAP,5000,ST,Track,RECORDING,STOPPED",  # intended take (ends in a transport stop)
            "#CAP,1200,MI,U,144,5,60,100",
            "#CAP,2200,MI,U,144,5,62,100",
            "#CAP,9000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,9500,ST,Track,OVERDUBBING,PLAYING",
            "#CAP,30000,ST,Track,ARMED,RECORDING",
            "#CAP,30800,ST,Track,RECORDING,STOPPED_RECORDING",  # stray re-arm, no notes
        ]
        report = extract_phase_sessions(lines)
        self.assertEqual(report.record.start_ts, 1000)
        self.assertEqual(report.record.stop_ts, 5000)

    def test_no_host_notes_keeps_the_first_take_and_says_so(self) -> None:
        lines = [
            "#CAP,1000,ST,Track,ARMED,RECORDING",
            "#CAP,2000,ST,Track,RECORDING,STOPPED_RECORDING",
            "#CAP,3000,ST,Track,ARMED,RECORDING",
            "#CAP,4000,ST,Track,RECORDING,STOPPED_RECORDING",
            "#CAP,5000,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,6000,ST,Track,OVERDUBBING,PLAYING",
        ]
        report = extract_phase_sessions(lines)
        # Both takes are armed and neither holds notes: the earliest armed take stays the record leg.
        self.assertEqual(report.record.start_ts, 1000)
        self.assertTrue(any("chosen without host note rows" in d for d in report.diagnostics))


class ExtractFirstNoteOffsetTests(unittest.TestCase):
    def test_offset_from_us_delta_at_120_bpm(self) -> None:
        # 120 BPM → 20833 us per clock; 2 clocks ≈ 41666 us
        lines = [
            "#CAP,1000000,ST,Track,ARMED,RECORDING",
            "#CAP,1041666,MI,U,144,5,60,100",
        ]
        result = extract_first_note_offset(
            lines,
            midi_channel_1based=5,
            phase_start_ts=1_000_000,
            phase_stop_ts=2_000_000,
            fallback_bpm=120.0,
        )
        self.assertEqual(result["offset_us"], 41666)
        self.assertEqual(result["offset_clocks"], 2)
        self.assertEqual(result["capture_latency_clocks"], 2)
        self.assertFalse(result["phase_missing"])

    def test_subtracts_configured_phase_start_delay(self) -> None:
        # Default overdub: 1 beat delay (24 clocks); first note at 25 clocks → latency 1
        delay = phase_start_delay_clocks(delay_bars=0, delay_beats=1)
        self.assertEqual(delay, 24)
        us_per_clock = 60_000_000.0 / (120.0 * 24.0)
        phase_start = 1_000_000
        first_note_ts = phase_start + int(round(25 * us_per_clock))
        lines = [
            f"#CAP,{phase_start},ST,Track,PLAYING,OVERDUBBING",
            f"#CAP,{first_note_ts},MI,U,144,5,24,100",
        ]
        result = extract_first_note_offset(
            lines,
            midi_channel_1based=5,
            phase_start_ts=phase_start,
            phase_stop_ts=phase_start + 10_000_000,
            fallback_bpm=120.0,
            phase_start_delay_clocks=delay,
        )
        self.assertEqual(result["offset_clocks"], 25)
        self.assertEqual(result["capture_latency_clocks"], 1)
        self.assertEqual(result["phase_start_delay_clocks"], 24)

    def test_uses_serial_bpm_in_phase_window(self) -> None:
        lines = [
            "#CAP,1000000,ST,Track,ARMED,RECORDING",
            "#CAP,1002000,BPM,60.0,60.0",
            "#CAP,1010000,MI,U,144,5,60,100",
        ]
        result = extract_first_note_offset(
            lines,
            midi_channel_1based=5,
            phase_start_ts=1_000_000,
            phase_stop_ts=2_000_000,
            fallback_bpm=120.0,
        )
        # 60 BPM → 41666.67 us/clock; 10000 us ≈ 0.24 clocks → rounds to 0
        self.assertEqual(result["offset_us"], 10_000)
        self.assertEqual(result["offset_clocks"], 0)

    def test_missing_first_note(self) -> None:
        lines = ["#CAP,1000000,ST,Track,ARMED,RECORDING"]
        result = extract_first_note_offset(
            lines,
            midi_channel_1based=5,
            phase_start_ts=1_000_000,
            phase_stop_ts=2_000_000,
            fallback_bpm=120.0,
        )
        self.assertIsNone(result["offset_clocks"])

    def test_rejects_note_on_velocity_zero(self) -> None:
        lines = [
            "#CAP,1000000,ST,Track,ARMED,RECORDING",
            "#CAP,1041666,MI,U,144,5,60,0",
            "#CAP,1083332,MI,U,144,5,60,100",
        ]
        result = extract_first_note_offset(
            lines,
            midi_channel_1based=5,
            phase_start_ts=1_000_000,
            phase_stop_ts=2_000_000,
            fallback_bpm=120.0,
        )
        self.assertEqual(result["offset_clocks"], 4)
        self.assertEqual(result["first_note_ts"], 1083332)

    def test_does_not_use_whole_log_bpm_outside_phase(self) -> None:
        lines = [
            "#CAP,100,BPM,60.0,60.0",
            "#CAP,1000000,ST,Track,ARMED,RECORDING",
            "#CAP,1041666,MI,U,144,5,60,100",
        ]
        result = extract_first_note_offset(
            lines,
            midi_channel_1based=5,
            phase_start_ts=1_000_000,
            phase_stop_ts=2_000_000,
            fallback_bpm=120.0,
        )
        self.assertEqual(result["bpm_source"], "fallback_cli")
        self.assertEqual(result["offset_clocks"], 2)


class MidiBoundaryTimingTests(unittest.TestCase):
    def test_stopping_and_following_playing_are_separate_active_intervals(self) -> None:
        lines = [
            "#HITL,SCENARIO,deferred_stop",
            "#CAP,100,ST,Track,STOPPED,PLAYING",
            "#CAP,200,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,300,ST,Track,OVERDUBBING,OVERDUB_STOPPING",
            "#CAP,400,ST,Track,OVERDUB_STOPPING,PLAYING",
            "#CAP,500,ST,Track,PLAYING,STOPPED",
        ]
        summary = summarize_midi_boundary_timing(lines)
        self.assertEqual(summary["interval_counts"], {
            "OVERDUBBING": 1,
            "OVERDUB_STOPPING": 1,
            "PLAYING": 2,
            "RECORDING": 0,
        })
        self.assertEqual(summary["overdub_stopping_diagnostics"], [])
        self.assertEqual(summary["overdub_stopping_segments"], [
            {
                "overdub_start_ts": 200,
                "stopping_start_ts": 300,
                "destination_ts": 400,
                "destination": "PLAYING",
            }
        ])
        self.assertEqual(midi_boundary_timing_issues(summary), [])

    def test_overflow_is_rejected_in_stopping_and_following_playing(self) -> None:
        lines = [
            "#CAP,100,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,200,ST,Track,OVERDUBBING,OVERDUB_STOPPING",
            "#CAP,250,RING,overflow",
            "#CAP,300,ST,Track,OVERDUB_STOPPING,PLAYING",
            "#CAP,350,RING,overflow",
            "#CAP,400,ST,Track,PLAYING,STOPPED",
        ]
        summary = summarize_midi_boundary_timing(lines)
        self.assertEqual(summary["overflow_intervals"], [
            {"state": "OVERDUB_STOPPING", "timestamp": 250},
            {"state": "PLAYING", "timestamp": 350},
        ])
        self.assertIn("ring_overflow_in_active_interval", midi_boundary_timing_issues(summary))

    def test_overflow_without_an_interval_still_invalidates_the_capture(self) -> None:
        summary = summarize_midi_boundary_timing(["#CAP,250,RING,overflow"])
        self.assertEqual(summary["overflow_timestamps"], [250])
        self.assertFalse(summary["overflow_free"])
        issues = midi_boundary_timing_issues(summary)
        self.assertIn("capture_ring_overflow", issues)
        self.assertNotIn("ring_overflow_in_active_interval", issues)

    def test_overflow_uses_the_first_eviction_timestamp_when_available(self) -> None:
        summary = summarize_midi_boundary_timing(
            [
                "#CAP,100,ST,Track,PLAYING,OVERDUBBING",
                "#CAP,200,ST,Track,OVERDUBBING,PLAYING",
                "#CAP,900,RING,overflow,150",
            ]
        )
        self.assertEqual(summary["overflow_timestamps"], [150])
        self.assertEqual(
            summary["overflow_intervals"], [{"state": "OVERDUBBING", "timestamp": 150}]
        )

    def test_direct_legacy_transition_cannot_satisfy_stopping_fixture(self) -> None:
        lines = [
            "#CAP,100,ST,Track,PLAYING,OVERDUBBING",
            "#CAP,200,ST,Track,OVERDUBBING,PLAYING",
        ]
        segments, diagnostics = extract_overdub_stopping_segments(lines)
        self.assertEqual(segments, [])
        self.assertEqual(diagnostics, ["legacy_direct_overdub_stop"])

    def test_current_firmware_has_only_the_midi_input_call_site_allowlist(self) -> None:
        summary = summarize_midi_boundary_timing(
            [], repo_root=_SCRIPT_DIR.parent
        )
        self.assertTrue(summary["call_site_gate_ok"])
        self.assertEqual(summary["extra_midi_input_call_sites"], [])


class DeadlineCorrelationTests(unittest.TestCase):
    def test_delayed_notice_uses_send_state_and_preserves_legacy_uncertainty(self):
        rows = [
            "#CAP,100,ST,Track,OVERDUBBING,OVERDUB_STOPPING",
            "#CAP,300,ST,Track,OVERDUB_STOPPING,PLAYING",
            "#CAP,500,ST,Track,PLAYING,STOPPED",
            "#CAP,450,DIAG,late_event,off,10,42,250,200,240",
            "#CAP,450,DIAG,late_event,off,10,42",
        ]
        samples = extract_deadline_samples(rows)
        self.assertEqual(samples[0]["state"], "OVERDUB_STOPPING")
        self.assertTrue(samples[0]["correlation_valid"])
        self.assertIsNone(samples[1]["state"])
        self.assertFalse(samples[1]["correlation_valid"])
        self.assertFalse(summarize_midi_boundary_timing(rows)["deadline_safety_proven"])

    def test_inconsistent_deadline_is_not_attributed(self):
        sample = extract_deadline_samples(["#CAP,450,DIAG,late_event,on,10,42,250,200,230"])[0]
        self.assertFalse(sample["correlation_valid"])
        self.assertIsNone(sample["state"])

    def test_stop_phase_summary_preserves_counts_and_max_span(self):
        samples = extract_stop_phase_samples([
            "#CAP,500,DIAG,stop_phase,4,0,2,1,0,0,3,1,0,100,130,20",
            "#CAP,500,DIAG,stop_phase,4,0,2,1,0,0,3,4,0,100,130,20",
        ])
        self.assertTrue(samples[0]["valid"])
        self.assertEqual(samples[0]["calls"], 3)
        self.assertEqual(samples[0]["crossed"], 1)
        self.assertEqual(samples[0]["max_duration_us"], 30)
        self.assertFalse(samples[1]["valid"])

    def test_stop_timeline_correlates_device_clock_grains_services_and_emissions(self):
        report = extract_stop_timeline([
            "#CAP,495,DIAG,stop_state,4,0,6,7,92,95,93,90,110,20",
            "#CAP,500,DIAG,stop_grain,4,0,2,1,0,0,93,100,130,90,90,110,20,10,0",
            "#CAP,510,DIAG,stop_service,94,121,110,130,20",
            "#CAP,520,DIAG,stop_emit,94,off,48,122,110,130,0",
            "#CAP,530,DIAG,stop_grain,4,0,2,1,0,0,94,131,140,121,110,130,20,0,1",
            "#CAP,535,DIAG,stop_state,4,0,7,5,95,141,94,110,130,20",
        ])
        self.assertTrue(report["correlation_complete"])
        self.assertTrue(report["state_correlation_complete"])
        self.assertEqual(len(report["grains"]), 2)
        self.assertEqual(report["emissions"][0]["sequence"], 94)
        self.assertEqual(report["grains"][0]["deadline_relation"], "crossed_during_grain")
        self.assertTrue(report["grains"][0]["overlaps_next_observed_service"])
        self.assertEqual(report["grains"][1]["deadline_relation"], "already_overdue_at_start")
        self.assertIsNone(report["grains"][1]["next_observed_service_us"])

    def test_stop_emission_retains_owner_and_rejects_invalid_identity(self):
        service = "#CAP,510,DIAG,stop_service,94,121,110,130,20"
        emission = "#CAP,520,DIAG,stop_emit,94,on,48,122,110,130,0,4,0,5,71"
        report = extract_stop_timeline([service, emission])
        self.assertTrue(report["emissions"][0]["valid"])
        self.assertEqual(report["emissions"][0]["track"], 4)
        self.assertEqual(report["emissions"][0]["channel"], 5)
        self.assertEqual(report["emissions"][0]["note"], 71)
        bad = extract_stop_timeline([service, emission.rsplit(",", 1)[0] + ",128"])
        self.assertFalse(bad["emissions"][0]["valid"])

    def test_stop_timeline_rejects_invalid_or_reversed_state_edges(self):
        grain = "#CAP,500,DIAG,stop_grain,4,0,1,3,4,0,93,100,105,90,90,110,20,10,1"
        entry = "#CAP,495,DIAG,stop_state,4,0,6,7,92,95,93,90,110,20"
        invalid = "#CAP,535,DIAG,stop_state,4,0,7,5,95,141,94,110,131,20"
        report = extract_stop_timeline([grain, entry, invalid])
        self.assertFalse(report["correlation_complete"])
        self.assertFalse(report["state_correlation_complete"])
        reversed_edge = "#CAP,535,DIAG,stop_state,4,0,7,5,95,94,94,110,130,20"
        report = extract_stop_timeline([grain, entry, reversed_edge])
        self.assertFalse(report["state_correlation_complete"])

    def test_stop_timeline_drop_invalidates_trace_evidence(self):
        summary = summarize_midi_boundary_timing([
            "#CAP,500,DIAG,stop_trace_drop,1",
        ])
        self.assertIn("stop_timeline_trace_drop", midi_boundary_timing_issues(summary))

    def test_wrapped_device_timestamp_is_attributed(self):
        rows = ["#CAP,4294967280,ST,Track,OVERDUBBING,OVERDUB_STOPPING",
                "#CAP,30,ST,Track,OVERDUB_STOPPING,PLAYING",
                "#CAP,50,DIAG,late_event,clk,10,42,5,4294967280,4294967291"]
        sample = extract_deadline_samples(rows)[0]
        self.assertTrue(sample["correlation_valid"])
        self.assertEqual(sample["state"], "OVERDUB_STOPPING")


class ManagedCaptureTests(unittest.TestCase):
    def test_boot_ready_accepts_capture_sessions_relative_pointer(self) -> None:
        with tempfile.TemporaryDirectory(dir=_SCRIPT_DIR.parent) as temp_dir:
            out_dir = Path(temp_dir) / "captures"
            out_dir.mkdir()
            session_log = out_dir / "session.log"
            session_log.touch()
            (out_dir / ".current_session.ready").write_text(
                str(session_log.relative_to(_SCRIPT_DIR.parent)) + "\n",
                encoding="utf-8",
            )
            wait_for_capture_boot_ready(out_dir, session_log, timeout_s=0.1)


if __name__ == "__main__":
    unittest.main()
