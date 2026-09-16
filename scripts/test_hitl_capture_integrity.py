#!/usr/bin/env python3
"""Stage 2 HITL capture integrity and liveness tests."""

from __future__ import annotations

import sys
import tempfile
import time
import unittest
from types import SimpleNamespace
from unittest.mock import patch
from contextlib import contextmanager
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.config import HitlConfig
from hitl.contract import CaptureIntegrity
from hitl.managed_capture import (
    ManagedCaptureSession,
    active_managed_capture,
    legacy_args_with_managed_capture,
    run_with_managed_capture,
)
from hitl.serial_collector import (
    CaptureLineClock,
    RunAbort,
    serial_line_is_transport,
    serial_line_resets_heartbeat,
)
from hitl.serial_follow import ExternalSerialFollowCollector


class CaptureLineClockTests(unittest.TestCase):
    def test_dframe_updates_reader_and_transport_not_firmware(self) -> None:
        clock = CaptureLineClock()
        clock.note_line("#CAP,1,DFRAME,0,9270,33120", now=10.0)
        self.assertEqual(clock.integrity, CaptureIntegrity.COMPLETE)
        self.assertEqual(clock.last_reader_at, 10.0)
        self.assertEqual(clock.last_transport_at, 10.0)
        self.assertIsNone(clock.last_firmware_at)
        self.assertFalse(serial_line_resets_heartbeat("#CAP,1,DFRAME,0,9270,33120"))
        self.assertTrue(serial_line_is_transport("#CAP,1,DFRAME,0,9270,33120"))

    def test_st_updates_firmware_and_transport(self) -> None:
        clock = CaptureLineClock()
        clock.note_line("#CAP,1,ST,Track,EMPTY,PLAYING", now=12.0)
        self.assertEqual(clock.last_firmware_at, 12.0)
        self.assertEqual(clock.last_transport_at, 12.0)
        self.assertEqual(clock.last_reader_at, 12.0)

    def test_pers_work_is_reader_only(self) -> None:
        clock = CaptureLineClock()
        clock.note_line("#CAP,1,PERS,work,ok", now=8.0)
        self.assertEqual(clock.last_reader_at, 8.0)
        self.assertIsNone(clock.last_firmware_at)
        self.assertIsNone(clock.last_transport_at)

    def test_incomplete_reason_aborts_without_timeout(self) -> None:
        clock = CaptureLineClock()
        clock.note_line("#CAP,1,ST,Track,EMPTY,PLAYING", now=1.0)
        clock.mark_incomplete("capture file truncated (12 < offset 40)")
        self.assertEqual(clock.integrity, CaptureIntegrity.INCOMPLETE)
        self.assertIn("truncated", clock.integrity_abort_reason() or "")

    def test_ring_overflow_marks_capture_incomplete(self) -> None:
        clock = CaptureLineClock()
        clock.note_line("#CAP,123,RING,overflow", now=1.0)
        self.assertEqual(clock.integrity, CaptureIntegrity.INCOMPLETE)
        self.assertEqual(clock.integrity_abort_reason(), "firmware capture ring overflow")


class FollowCaptureIntegrityTests(unittest.TestCase):
    def test_truncation_marks_incomplete_and_aborts(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log_path = Path(tmp) / "session.log"
            log_path.write_text("#CAP,1,ST,Track,EMPTY,PLAYING\n", encoding="utf-8")
            collector = ExternalSerialFollowCollector(log_path, poll_interval_s=0.02)
            collector.start()
            try:
                deadline = time.monotonic() + 2.0
                while time.monotonic() < deadline:
                    if any("PLAYING" in line for line in collector.snapshot()):
                        break
                    time.sleep(0.02)
                self.assertEqual(collector.capture_integrity(), CaptureIntegrity.COMPLETE)
                log_path.write_text("short\n", encoding="utf-8")
                deadline = time.monotonic() + 2.0
                while time.monotonic() < deadline:
                    if collector.capture_integrity() is CaptureIntegrity.INCOMPLETE:
                        break
                    time.sleep(0.02)
                self.assertEqual(collector.capture_integrity(), CaptureIntegrity.INCOMPLETE)
                reason = collector.capture_abort_reason(timeout_s=30.0)
                self.assertIsNotNone(reason)
                self.assertIn("truncated", reason or "")
                abort = RunAbort(serial_collector=collector, heartbeat_timeout_s=30.0)
                self.assertIn("truncated", abort.check() or "")
            finally:
                collector.stop()

    def test_rotation_marks_incomplete(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out_dir = Path(tmp)
            log_path = out_dir / "session_a.log"
            rotated = out_dir / "session_b.log"
            log_path.write_text("#CAP,1,ST,Track,EMPTY,PLAYING\n", encoding="utf-8")
            rotated.write_text("#CAP,1,ST,Track,EMPTY,PLAYING\n", encoding="utf-8")
            pointer = out_dir / ".current_session"
            pointer.write_text(str(log_path) + "\n", encoding="utf-8")
            collector = ExternalSerialFollowCollector(
                log_path, poll_interval_s=0.02, out_dir=out_dir
            )
            collector.start()
            try:
                deadline = time.monotonic() + 2.0
                while time.monotonic() < deadline:
                    if collector.capture_integrity() is CaptureIntegrity.COMPLETE:
                        break
                    time.sleep(0.02)
                self.assertEqual(collector.capture_integrity(), CaptureIntegrity.COMPLETE)
                pointer.write_text(str(rotated) + "\n", encoding="utf-8")
                deadline = time.monotonic() + 2.0
                while time.monotonic() < deadline:
                    if collector.capture_integrity() is CaptureIntegrity.INCOMPLETE:
                        break
                    time.sleep(0.02)
                self.assertEqual(collector.capture_integrity(), CaptureIntegrity.INCOMPLETE)
                self.assertIn("rotated", collector.capture_integrity_reason())
            finally:
                collector.stop()

    def test_child_exit_marks_incomplete_immediately(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log_path = Path(tmp) / "session.log"
            log_path.write_text("#CAP,1,ST,Track,EMPTY,PLAYING\n", encoding="utf-8")
            collector = ExternalSerialFollowCollector(
                log_path,
                poll_interval_s=0.02,
                child_poll=lambda: 1,
            )
            collector.start()
            try:
                reason = collector.capture_abort_reason(timeout_s=30.0)
                self.assertIsNotNone(reason)
                self.assertIn("child exited", reason or "")
                self.assertEqual(collector.capture_integrity(), CaptureIntegrity.INCOMPLETE)
            finally:
                collector.stop()

    def test_dframe_resets_transport_not_firmware_age(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log_path = Path(tmp) / "session.log"
            log_path.write_text("#CAP,1,ST,Track,EMPTY,PLAYING\n", encoding="utf-8")
            collector = ExternalSerialFollowCollector(log_path, poll_interval_s=0.02)
            collector.start()
            try:
                deadline = time.monotonic() + 2.0
                while time.monotonic() < deadline:
                    if collector.seconds_since_last_line() is not None:
                        break
                    time.sleep(0.02)
                time.sleep(0.12)
                with log_path.open("a", encoding="utf-8") as log_file:
                    log_file.write("#CAP,2,DFRAME,0,9270,33120\n")
                deadline = time.monotonic() + 2.0
                while time.monotonic() < deadline:
                    age = collector.seconds_since_last_transport_line()
                    if age is not None and age < 0.1:
                        break
                    time.sleep(0.02)
                firmware_age = collector.seconds_since_last_line()
                transport_age = collector.seconds_since_last_transport_line()
                reader_age = collector.seconds_since_last_reader_line()
                self.assertIsNotNone(firmware_age)
                self.assertIsNotNone(transport_age)
                self.assertIsNotNone(reader_age)
                self.assertGreater(firmware_age or 0.0, transport_age or 0.0)
                self.assertLess(reader_age or 1.0, 0.2)
            finally:
                collector.stop()

    def test_partial_line_does_not_drop_publish_stage(self) -> None:
        """Growing-file readline without \\n must not advance offset past the line."""
        with tempfile.TemporaryDirectory() as tmp:
            log_path = Path(tmp) / "session.log"
            log_path.write_bytes(b"#CAP,1,RECS,stage,seal,1,0,0,0,0,0,ok\n")
            collector = ExternalSerialFollowCollector(log_path, poll_interval_s=0.02)
            collector.start()
            try:
                deadline = time.monotonic() + 2.0
                while time.monotonic() < deadline:
                    if any("seal" in line for line in collector.snapshot()):
                        break
                    time.sleep(0.02)
                # Append an incomplete CAP line (no terminator), then complete it.
                with log_path.open("ab") as log_file:
                    log_file.write(b"#CAP,2,RECS,stage,publish,2,0,0,0,0,0,ok")
                    log_file.flush()
                time.sleep(0.08)
                self.assertFalse(
                    any("publish" in line for line in collector.snapshot()),
                    "partial publish line must not be consumed yet",
                )
                with log_path.open("ab") as log_file:
                    log_file.write(b"\n#CAP,3,RECS,stage,finalize,3,0,0,0,0,0,published\n")
                    log_file.flush()
                deadline = time.monotonic() + 2.0
                saw_publish = False
                saw_finalize = False
                while time.monotonic() < deadline:
                    snap = collector.snapshot()
                    saw_publish = any("RECS,stage,publish" in line and ",ok" in line for line in snap)
                    saw_finalize = any("finalize" in line for line in snap)
                    if saw_publish and saw_finalize:
                        break
                    time.sleep(0.02)
                self.assertTrue(saw_publish, collector.snapshot()[-5:])
                self.assertTrue(saw_finalize, collector.snapshot()[-5:])
            finally:
                collector.stop()


class ManagedCaptureActiveTests(unittest.TestCase):
    def test_managed_callback_gets_the_exact_new_log(self) -> None:
        config = HitlConfig(preset="base", scenario_ids=("base",), track_number=5,
            loop_slot=1, midi_channel=5, record_bars=2, overdub_bars=2,
            midi_out_name="Teensy", midi_in_name="Teensy", serial_port=None,
            verify_serial_log=None, verify_only=False, out_dir=Path("captures/custom"))
        target = config.out_dir / "new-session.log"
        @contextmanager
        def session(_config):
            yield target
        def check(active):
            self.assertEqual(target, active.follow_serial_log)
            self.assertFalse(active.follow_current_session)
            self.assertEqual(("--follow-serial-log", str(target)), active.legacy_extra_args)
            return 17
        with patch("hitl.managed_capture.managed_capture_session", session), \
                patch("hitl.managed_capture.write_metadata_best_effort"):
            self.assertEqual(17, run_with_managed_capture(config, check))


    def test_exact_managed_log_replaces_stale_pointer_and_explicit_log(self) -> None:
        args = SimpleNamespace(verify_only=False, no_managed_capture=False)
        target = Path("captures/custom/current-run.log")
        for stale in (["--follow-current-session"],
                      ["--follow-serial-log", "captures/old.log"],
                      ["--follow-serial-log=captures/old.log"]):
            result = legacy_args_with_managed_capture(["--track", "5"] + stale, args, target)
            self.assertEqual(["--track", "5", "--follow-serial-log", str(target)], result)


    def test_poll_none_when_process_missing(self) -> None:
        session = ManagedCaptureSession(out_dir=Path("captures"))
        self.assertIsNone(session.poll())
        self.assertIsNone(session.child_exit_reason())
        self.assertIsNone(active_managed_capture())


if __name__ == "__main__":
    unittest.main()
