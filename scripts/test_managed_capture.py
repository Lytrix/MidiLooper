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

    def test_watchdog_cancels_a_registered_run_and_joins_it_before_release(self) -> None:
        """A registered run is cancelled and *joined* before the watchdog returns (the capture is released after).

        The scenario here is what a driver looks like: it holds a note, sleeps in slices behind its scope, and
        releases what it was holding when the cancel arrives. The assertions are the requirement's order --
        cancel, then the run leaves the wire, then the capture is released -- plus "nothing is sent after the
        abort" and the bounded grace.
        """
        import time as _time

        import mido

        from hitl.actions.cancellation import (
            CancelScope,
            GuardedMidiOut,
            RunCancelled,
            begin_run,
            cancellable_sleep,
            end_run,
        )
        from hitl.managed_capture import _run_with_watchdog

        class Port:
            def __init__(self) -> None:
                self.sent = []

            def send(self, message) -> None:
                self.sent.append(message)

        port = Port()
        scope = begin_run(scope=CancelScope(name="watchdog-test"))
        guard = GuardedMidiOut(port, scope=scope)
        seen: dict[str, object] = {}

        def scenario() -> int:
            guard.send(mido.Message("note_on", channel=3, note=60, velocity=100))
            try:
                while True:
                    cancellable_sleep(0.05, scope=scope)
            except RunCancelled:
                try:
                    guard.send(mido.Message("note_on", channel=3, note=61, velocity=100))
                except RunCancelled:
                    seen["refused_after_cancel"] = True
                guard.release_held_notes()
            # A run needs *time* to leave the wire, and the grace is what the watchdog waits for: this takes a
            # slice of it before recording that the run stopped. Without the join, the watchdog would return
            # while the scenario was still inside this sleep.
            _time.sleep(0.3)
            seen["stopped"] = True
            return 3

        try:
            started = _time.monotonic()
            code = _run_with_watchdog(scenario, 0.25, grace_s=5.0)
            elapsed = _time.monotonic() - started
        finally:
            end_run(scope)
        self.assertEqual(3, code)
        self.assertTrue(seen.get("stopped"), "the cancelled run was joined before the watchdog returned")
        self.assertTrue(seen.get("refused_after_cancel"), "a post-cancel send must be refused")
        self.assertEqual(["note_on", "note_off", "control_change", "control_change"],
                         [message.type for message in port.sent],
                         "only the held note was released; the refused edge never went out")
        self.assertLess(elapsed, 6.0, "the watchdog stays bounded after cancelling")
        self.assertTrue(scope.cancelled)
        self.assertIn("watchdog", scope.reason or "")

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


    def test_managed_run_writes_metadata(self) -> None:
        import json
        from contextlib import contextmanager
        from unittest.mock import patch

        from hitl.managed_capture import run_with_managed_capture

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            config = HitlConfig(
                preset="base",
                scenario_ids=("base",),
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
                out_dir=out,
                build_env="__no_such_env__",
            )
            # The capture always leaves a real log (the stream check reads it), so the fixture does
            # too: an attaching run needs no boot banner, only a readable stream.
            target = out / "new-session.log"
            target.write_text("BOOT,load,ok\n#CAP,1,ST,Track,STOPPED,STOPPED\n", encoding="utf-8")

            @contextmanager
            def session(_config):
                yield target

            with patch("hitl.managed_capture.managed_capture_session", session):
                self.assertEqual(0, run_with_managed_capture(config, lambda _active: 0, timeout_s=0))

            meta = json.loads((out / "metadata.json").read_text())
            self.assertEqual(meta["preset"], "base")
            self.assertEqual(meta["scenario_ids"], ["base"])
            self.assertEqual(meta["session_log"], str(target))
            self.assertIsNone(meta["firmware_hex_sha256"])


if __name__ == "__main__":
    unittest.main()
