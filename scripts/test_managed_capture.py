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
    def setUp(self) -> None:
        # These fixtures own bootstrap/orchestration only, not device shutdown.
        # test_hitl_live_observation drives the real DeviceSession separately.
        from types import SimpleNamespace
        from unittest.mock import patch
        class ShutdownBoundary:
            def __init__(self, **_kwargs):
                self.result = SimpleNamespace(ok=True, as_dict=lambda: dict(ok=True))
            def __enter__(self):
                return self
            def __exit__(self, *_args):
                return False
        self.enterContext(patch('hitl.device_shutdown.DeviceSession', ShutdownBoundary))

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

    def test_boot_bound_capture_restarts_after_the_idempotent_stop(self) -> None:
        """The real method, not a stand-in: a forced capture with no banner stops then restarts.

        The old guard refused whenever the capture's own mode was already `force` -- i.e. exactly for
        the `--boot-reset` run whose reset the firmware had just refused because the transport was
        running, which is the state the repair exists for (`captures/live-current-root-entry-t8s4-16`).
        """
        from hitl.managed_capture import ManagedCaptureSession

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            log = out / "session.log"
            log.write_text("line\nline\n", encoding="utf-8")  # no boot banner yet
            session = ManagedCaptureSession(out_dir=out, boot_reset="force")
            session._session_log = log
            calls: list[str] = []
            session.stop = lambda: calls.append("capture_stop")
            session.start = lambda: (calls.append("capture_start"), log)[1]
            self.assertTrue(session.restart_with_boot_reset(lambda: calls.append("midi_stop")))
            self.assertEqual(["midi_stop", "capture_stop", "capture_start"], calls)
            # Bounded to one restart per run, and never a reboot of a capture that already booted.
            self.assertFalse(session.restart_with_boot_reset(lambda: calls.append("midi_stop")))


    def test_forced_boot_stops_the_transport_before_asking_for_the_reset(self) -> None:
        """A `--boot-reset` run on a device left playing must still get its boot.

        Regression (`captures/live-current-root-entry-t8s4-16`): the capture asked `!REBOOT`, the
        firmware refused it because the transport was still running, and the run then demanded a fresh
        stream from a capture that had never rebooted -- 2 lines -- so it refused at the stream check
        before anything stopped the transport. Readiness answered **ready** in that run ("no command
        replies while the transport runs; 8 fresh device row(s)": a playing device answers no query but
        does emit rows), so neither `not ready` nor a repair stop could tell that this run still owed a
        boot -- only the forced mode itself can. The stop that clears the `!REBOOT` gate is the
        idempotent MIDI Stop, and it is handed in only on this boot-bound path.
        """
        from contextlib import contextmanager
        from unittest.mock import patch

        from hitl import managed_capture as mc

        events: list[str] = []

        class FakeSession:
            def __init__(self, log):
                self.log = log
                self.session_log = None
                self._booted = False

            def boot_banner_seen(self):
                return self._booted

            def restart_with_boot_reset(self, stop_transport=None):
                events.append("restart")
                if self._booted:
                    return False
                if stop_transport is not None:
                    stop_transport()
                self._booted = True
                self.log.write_text("BOOT,load,ok\n" + "\n".join(
                    f"#CAP,{i},DFRAME,0,0,0,0,0,0" for i in range(60)) + "\n", encoding="utf-8")
                self.session_log = self.log
                return True

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            target = out / "session.log"
            target.write_text("line\nline\n", encoding="utf-8")  # a playing device says almost nothing
            session = FakeSession(target)
            config = HitlConfig(
                preset="base", scenario_ids=("base",), track_number=8, loop_slot=4,
                midi_channel=8, record_bars=2, overdub_bars=2, midi_out_name="Teensy",
                midi_in_name="Teensy", serial_port=None, verify_serial_log=None,
                verify_only=False, out_dir=out, build_env="__no_such_env__", boot_reset="force",
            )

            @contextmanager
            def managed(_config):
                yield target

            def stop_sender(_config, sent):
                def send():
                    if sent.get("value"):
                        return
                    sent["value"] = True
                    events.append("stop")
                return send

            def readiness(**_kwargs):
                # Run-16's shape: a playing device answers no query but does emit rows, so readiness
                # said *ready* and the boot this run forced was still owed.
                return True, "no command replies while the transport runs; 8 fresh device row(s)"

            with patch.object(mc, "managed_capture_session", managed), \
                    patch.object(mc, "active_managed_capture", lambda: session), \
                    patch.object(mc, "establish_attach_readiness", readiness), \
                    patch.object(mc, "transport_stop_sender", stop_sender):
                code = mc.run_with_managed_capture(config, lambda _active: 0, timeout_s=0)
        self.assertEqual(0, code)
        self.assertEqual(["restart", "stop"], events)

    def test_attach_without_a_boot_request_never_stops_the_transport(self) -> None:
        """The other half of the contract: a plain attach must not change the transport state."""
        from contextlib import contextmanager
        from unittest.mock import patch

        from hitl import managed_capture as mc

        stops: list[str] = []

        class FakeSession:
            session_log = None

            def boot_banner_seen(self):
                return False

            def restart_with_boot_reset(self, stop_transport=None):
                stops.append("restart")
                return False

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            target = out / "session.log"
            target.write_text("BOOT,load,ok\n" + "\n".join(
                f"#CAP,{i},ST,Track,PLAYING,PLAYING" for i in range(60)) + "\n", encoding="utf-8")
            config = HitlConfig(
                preset="base", scenario_ids=("base",), track_number=8, loop_slot=4,
                midi_channel=8, record_bars=2, overdub_bars=2, midi_out_name="Teensy",
                midi_in_name="Teensy", serial_port=None, verify_serial_log=None,
                verify_only=False, out_dir=out, build_env="__no_such_env__", boot_reset="never",
            )

            @contextmanager
            def managed(_config):
                yield target

            def stop_sender(_config, sent):
                def send():
                    stops.append("stop")
                return send

            with patch.object(mc, "managed_capture_session", managed), \
                    patch.object(mc, "active_managed_capture", lambda: FakeSession()), \
                    patch.object(mc, "establish_attach_readiness",
                                 lambda **kwargs: (True, "attaching to a running device")), \
                    patch.object(mc, "transport_stop_sender", stop_sender):
                code = mc.run_with_managed_capture(config, lambda _active: 0, timeout_s=0)
        self.assertEqual(0, code)
        self.assertEqual([], stops)

if __name__ == "__main__":
    unittest.main()
