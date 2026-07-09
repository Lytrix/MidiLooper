#!/usr/bin/env python3
"""Unit tests for HITL transport reset and external serial follow."""

from __future__ import annotations

import sys
import tempfile
import time
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.serial_follow import ExternalSerialFollowCollector
from host_midi_automation_baseline import (
    _latest_track_state,
    _serial_has_recording_started,
    _serial_lines_show_bpm_activity,
    _serial_sequencer_running,
    _should_reset_transport_for_recovery,
    _suffix_shows_disp_recording,
)


class _FakeCollector:
    def __init__(self, lines: list[str], silence_s: float | None) -> None:
        self._lines = lines
        self._silence_s = silence_s

    def snapshot(self) -> list[str]:
        return list(self._lines)

    def seconds_since_last_line(self) -> float | None:
        return self._silence_s


class TransportResetRecoveryTests(unittest.TestCase):
    def test_skip_when_clock_present_and_idle(self) -> None:
        lines = ["#CAP,1,ST,Track,EMPTY,PLAYING,0"]
        self.assertFalse(
            _should_reset_transport_for_recovery(clock_present=True, serial_lines=lines)
        )

    def test_run_when_clock_missing(self) -> None:
        self.assertTrue(
            _should_reset_transport_for_recovery(clock_present=False, serial_lines=None)
        )

    def test_run_when_stuck_recording_even_with_clock(self) -> None:
        lines = ["#CAP,1,ST,Track,ARMED,RECORDING,0"]
        self.assertTrue(
            _should_reset_transport_for_recovery(clock_present=True, serial_lines=lines)
        )

    def test_run_when_stuck_overdubbing(self) -> None:
        lines = ["#CAP,1,ST,Track,PLAYING,OVERDUBBING,0"]
        self.assertTrue(
            _should_reset_transport_for_recovery(clock_present=True, serial_lines=lines)
        )

    def test_skip_when_serial_proxy_active(self) -> None:
        self.assertFalse(
            _should_reset_transport_for_recovery(
                clock_present=False,
                serial_lines=None,
                serial_proxy_active=True,
            )
        )


class SerialTransportProxyTests(unittest.TestCase):
    def test_bpm_activity_in_tail(self) -> None:
        lines = ["#CAP,1,MO,144,1,60,100", "#CAP,2,BPM,120.0,120.0"]
        self.assertTrue(_serial_lines_show_bpm_activity(lines))

    def test_sequencer_running_from_recent_bpm(self) -> None:
        collector = _FakeCollector(["#CAP,1,BPM,120.0,120.0"], silence_s=1.0)
        self.assertTrue(_serial_sequencer_running(collector))

    def test_sequencer_not_running_when_silent(self) -> None:
        collector = _FakeCollector(["#CAP,1,BPM,120.0,120.0"], silence_s=10.0)
        self.assertFalse(_serial_sequencer_running(collector))


class ExternalSerialFollowCollectorTests(unittest.TestCase):
    def test_tails_growing_log(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log_path = Path(tmp) / "session.log"
            log_path.write_text("line1\n", encoding="utf-8")
            collector = ExternalSerialFollowCollector(log_path, poll_interval_s=0.02)
            collector.start()
            try:
                deadline = time.monotonic() + 2.0
                while time.monotonic() < deadline:
                    if "line1" in collector.snapshot():
                        break
                    time.sleep(0.02)
                self.assertIn("line1", collector.snapshot())

                with log_path.open("a", encoding="utf-8") as log_file:
                    log_file.write("line2\n")
                deadline = time.monotonic() + 2.0
                while time.monotonic() < deadline:
                    snap = collector.snapshot()
                    if "line2" in snap:
                        break
                    time.sleep(0.02)
                self.assertIn("line2", collector.snapshot())
            finally:
                collector.stop()

    def test_bootstraps_tail_without_replaying_entire_log(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log_path = Path(tmp) / "session.log"
            filler = "\n".join(f"old line {index}" for index in range(5000))
            log_path.write_text(filler + "\n#CAP,1,ST,Track,EMPTY,PLAYING\n", encoding="utf-8")
            collector = ExternalSerialFollowCollector(
                log_path,
                poll_interval_s=0.02,
                bootstrap_tail_bytes=4096,
                bootstrap_tail_lines=50,
            )
            collector.start()
            try:
                deadline = time.monotonic() + 2.0
                while time.monotonic() < deadline:
                    snap = collector.snapshot()
                    if any("ST,Track,EMPTY,PLAYING" in line for line in snap):
                        break
                    time.sleep(0.02)
                snap = collector.snapshot()
                self.assertTrue(any("ST,Track,EMPTY,PLAYING" in line for line in snap))
                self.assertFalse(any(line.startswith("old line 0") for line in snap))
                with log_path.open("a", encoding="utf-8") as log_file:
                    log_file.write("#CAP,2,RECA,0,100\n")
                deadline = time.monotonic() + 2.0
                while time.monotonic() < deadline:
                    if any(",RECA," in line for line in collector.snapshot()):
                        break
                    time.sleep(0.02)
                self.assertTrue(any(",RECA," in line for line in collector.snapshot()))
            finally:
                collector.stop()

    def test_resolve_current_session_pointer(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            log_path = base / "session_test.log"
            log_path.write_text("#CAP,ping\n", encoding="utf-8")
            pointer = base / ".current_session"
            pointer.write_text(str(log_path) + "\n", encoding="utf-8")
            resolved = ExternalSerialFollowCollector.resolve_current_session_path(base)
            self.assertEqual(resolved.resolve(), log_path.resolve())


class RecordingSerialEvidenceTests(unittest.TestCase):
    def test_latest_track_state_prefers_newer_disp(self) -> None:
        lines = [
            "#CAP,100,ST,Track,EMPTY,PLAYING",
            "#CAP,200,DISP,0,RECORDING,8,0,0,0,0,0",
        ]
        self.assertEqual(_latest_track_state(lines), "RECORDING")

    def test_serial_has_recording_started_from_disp(self) -> None:
        lines = ["#CAP,1,ST,Track,EMPTY,PLAYING", "#CAP,2,DISP,0,RECORDING,1,0,0,0,0,0"]
        self.assertTrue(_suffix_shows_disp_recording(lines, after_index=1))
        self.assertTrue(_serial_has_recording_started(lines, after_index=1))


if __name__ == "__main__":
    unittest.main()
