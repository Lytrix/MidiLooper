#!/usr/bin/env python3
"""Unit tests for managed capture orchestration."""

from __future__ import annotations

import argparse
import sys
import tempfile
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.config import HitlConfig
from hitl.layered_cli import legacy_args_with_slot_flags, require_slot_target
from hitl.managed_capture import wait_for_current_session_pointer


class ManagedCaptureTests(unittest.TestCase):
    def test_wait_for_current_session_pointer(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out_dir = Path(tmp)
            pointer = out_dir / ".current_session"
            log_path = out_dir / "session_test.log"
            log_path.write_text("", encoding="utf-8")

            def _publish() -> None:
                import threading
                import time

                def _write() -> None:
                    time.sleep(0.05)
                    pointer.write_text(str(log_path) + "\n", encoding="utf-8")

                threading.Thread(target=_write, daemon=True).start()

            _publish()
            resolved = wait_for_current_session_pointer(out_dir, timeout_s=2.0)
            self.assertTrue(resolved.samefile(log_path))

    def test_require_slot_target_rejects_missing_flags(self) -> None:
        args = argparse.Namespace(track_number=None, loop_slot=None, midi_channel=None)
        with self.assertRaises(ValueError):
            require_slot_target(args, [])

    def test_legacy_args_include_track_loop_slot_and_channel(self) -> None:
        args = argparse.Namespace(track_number=5, loop_slot=2, midi_channel=5)
        merged = legacy_args_with_slot_flags(args, [])
        self.assertEqual(
            merged[-6:],
            ["--track-number", "5", "--loop-slot", "2", "--midi-channel", "5"],
        )

    def test_hitl_config_needs_managed_capture_by_default(self) -> None:
        config = HitlConfig(
            preset="base",
            scenario_ids=(),
            track_number=5,
            loop_slot=1,
            midi_channel=5,
            record_bars=2,
            overdub_bars=2,
            midi_out_name="Teensy",
            midi_in_name="Teensy",
            serial_port=None,
            verify_serial_log=None,
            verify_only=False,
            out_dir=Path("captures"),
        )
        self.assertTrue(config.needs_managed_capture())
        self.assertTrue(config.uses_external_serial_capture)

    def test_managed_session_poll_none_before_start(self) -> None:
        from hitl.managed_capture import ManagedCaptureSession, active_managed_capture

        session = ManagedCaptureSession(out_dir=Path("captures"))
        self.assertIsNone(session.poll())
        self.assertIsNone(active_managed_capture())

    def test_resolve_managed_run_timeout_default_and_env(self) -> None:
        import os

        from hitl.managed_capture import (
            DEFAULT_MANAGED_RUN_TIMEOUT_S,
            resolve_managed_run_timeout,
        )

        self.assertEqual(resolve_managed_run_timeout(12.0), 12.0)
        self.assertEqual(resolve_managed_run_timeout(-5.0), 0.0)
        self.assertEqual(DEFAULT_MANAGED_RUN_TIMEOUT_S, 360.0)
        old = os.environ.get("HITL_MANAGED_RUN_TIMEOUT_S")
        try:
            os.environ["HITL_MANAGED_RUN_TIMEOUT_S"] = "42"
            self.assertEqual(resolve_managed_run_timeout(), 42.0)
            os.environ["HITL_MANAGED_RUN_TIMEOUT_S"] = "not-a-number"
            self.assertEqual(resolve_managed_run_timeout(), DEFAULT_MANAGED_RUN_TIMEOUT_S)
            del os.environ["HITL_MANAGED_RUN_TIMEOUT_S"]
            self.assertEqual(resolve_managed_run_timeout(), DEFAULT_MANAGED_RUN_TIMEOUT_S)
        finally:
            if old is None:
                os.environ.pop("HITL_MANAGED_RUN_TIMEOUT_S", None)
            else:
                os.environ["HITL_MANAGED_RUN_TIMEOUT_S"] = old

    def test_watchdog_returns_run_code(self) -> None:
        from hitl.managed_capture import _run_with_watchdog

        self.assertEqual(_run_with_watchdog(lambda: 0, 5.0), 0)
        self.assertEqual(_run_with_watchdog(lambda: 2, 5.0), 2)

    def test_watchdog_aborts_stalled_run_and_returns(self) -> None:
        import time as _time

        from hitl.managed_capture import _run_with_watchdog

        started = _time.monotonic()
        code = _run_with_watchdog(lambda: _time.sleep(30) or 0, 0.25)
        elapsed = _time.monotonic() - started
        self.assertEqual(code, 3)
        self.assertLess(elapsed, 5.0, "watchdog must return promptly on a stall")

    def test_watchdog_disabled_when_nonpositive(self) -> None:
        from hitl.managed_capture import _run_with_watchdog

        self.assertEqual(_run_with_watchdog(lambda: 7, 0.0), 7)

    def test_watchdog_propagates_worker_exception(self) -> None:
        from hitl.managed_capture import _run_with_watchdog

        def _boom() -> int:
            raise RuntimeError("scenario failed")

        with self.assertRaises(RuntimeError):
            _run_with_watchdog(_boom, 5.0)

    def test_owner_pipe_stops_capture_when_owner_exits(self) -> None:
        import os as _os
        import subprocess
        import sys as _sys

        root = _SCRIPT_DIR.parent
        with tempfile.TemporaryDirectory() as tmp:
            out_dir = Path(tmp)
            read_fd, write_fd = _os.pipe()
            proc = subprocess.Popen(
                [
                    _sys.executable,
                    str(root / "scripts" / "capture_session.py"),
                    "--port", "/dev/null",
                    "--boot-wait", "0",
                    "--out-dir", str(out_dir),
                    "--reconnect-delay", "0.05",
                    "--owner-fd", str(read_fd),
                ],
                pass_fds=(read_fd,),
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            _os.close(read_fd)
            try:
                proc.wait(timeout=3.0)
                self.fail("capture_session must stay alive while the owner pipe is open")
            except subprocess.TimeoutExpired:
                pass
            _os.close(write_fd)  # owner exits
            try:
                proc.wait(timeout=10.0)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5.0)
                self.fail("orphaned capture_session must exit when the owner pipe closes")
            self.assertEqual(proc.returncode, 0)


if __name__ == "__main__":
    unittest.main()
