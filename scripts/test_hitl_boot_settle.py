#!/usr/bin/env python3
"""Tests for the HITL boot decision: attach when the state is right, else own a fresh boot.

The behaviour under test is the setup latency contract from the **setup-latency** slice of
`openspec/changes/response-driven-hitl-acceptance/tasks.md` (task 6.3's "three consecutive passes on
one boot" depends on it): the run must not wait on a fixed post-open window, must not wait on the
device's boot-load steps, and must reset only when the device is not already in the state the
scenario needs.

Evidence this pins:
  * `captures/s4f22-base-device-16` — a managed run spent its whole setup budget on the boot-time
    deferred save and never recorded (`reason: deferred_save_not_idle`), while the device was
    answering host-driven interaction ~130 s before its own `post_restore_census` marker. The boot
    load is therefore never the readiness gate, and the settle is never a fixed sleep.
  * `captures/s4f22-step10-g2` — an idle attached device refused readiness because `!STOP_STATUS`
    is transport-gated and a killed run had left the transport running.
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

import capture_session  # noqa: E402
from hitl import managed_capture as mc  # noqa: E402


class BootResetModeTests(unittest.TestCase):
    def test_default_mode_is_auto(self) -> None:
        self.assertEqual("auto", capture_session.normalize_boot_reset_mode(None))

    def test_legacy_booleans_keep_their_old_meaning(self) -> None:
        # `True` requested the reset; `False` asked for a plain attach and must not start resetting.
        self.assertEqual("force", capture_session.normalize_boot_reset_mode(True))
        self.assertEqual("never", capture_session.normalize_boot_reset_mode(False))

    def test_string_modes_pass_through_and_unknown_is_refused(self) -> None:
        self.assertEqual("auto", capture_session.normalize_boot_reset_mode("auto"))
        self.assertEqual("never", capture_session.normalize_boot_reset_mode("NEVER"))
        with self.assertRaises(ValueError):
            capture_session.normalize_boot_reset_mode("sometimes")

    def test_runner_flags_map_to_modes(self) -> None:
        import argparse

        from hitl.config import boot_reset_mode_from_args

        self.assertEqual("auto", boot_reset_mode_from_args(argparse.Namespace()))
        self.assertEqual("force", boot_reset_mode_from_args(argparse.Namespace(boot_reset=True)))
        self.assertEqual("never", boot_reset_mode_from_args(argparse.Namespace(no_boot_reset=True)))

    def test_managed_session_accepts_a_mode_and_a_legacy_boolean(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            self.assertEqual("auto", mc.ManagedCaptureSession(out_dir=out).boot_reset_mode)
            self.assertEqual(
                "never", mc.ManagedCaptureSession(out_dir=out, boot_reset=False).boot_reset_mode
            )
            self.assertEqual(
                "force", mc.ManagedCaptureSession(out_dir=out, boot_reset=True).boot_reset_mode
            )


class StateProbeTests(unittest.TestCase):
    def test_latest_row_decides(self) -> None:
        lines = [
            "#STOP_STATUS,us=1,state=PLAYING,pending=0,queue=0/4",
            "#STOP_STATUS,us=2,state=STOPPED,pending=0,queue=0/4",
        ]
        self.assertTrue(capture_session.device_reports_stopped(lines))

    def test_playing_or_absent_reply_is_not_stopped(self) -> None:
        self.assertFalse(
            capture_session.device_reports_stopped(["#STOP_STATUS,us=2,state=PLAYING,queue=0/4"])
        )
        self.assertFalse(
            capture_session.device_reports_stopped(["BOOT,load,start", "#DMA_XFER,1"])
        )


class SettleEndReasonTests(unittest.TestCase):
    def test_attach_without_a_reset_ends_immediately(self) -> None:
        self.assertEqual(
            "attach_no_reset",
            capture_session.settle_end_reason(
                reset_expected=False,
                saw_banner=False,
                saw_boot_load_ok=False,
                elapsed_s=0.02,
                ceiling_s=10.0,
            ),
        )

    def test_a_forced_boot_ends_on_the_boot_load_not_the_clock(self) -> None:
        self.assertIsNone(
            capture_session.settle_end_reason(
                reset_expected=True,
                saw_banner=True,
                saw_boot_load_ok=False,
                elapsed_s=2.0,
                ceiling_s=10.0,
            )
        )
        self.assertEqual(
            "boot_loaded",
            capture_session.settle_end_reason(
                reset_expected=True,
                saw_banner=True,
                saw_boot_load_ok=True,
                elapsed_s=2.4,
                ceiling_s=10.0,
            ),
        )

    def test_the_ceiling_only_bounds_a_device_that_never_gets_there(self) -> None:
        self.assertEqual(
            "ceiling",
            capture_session.settle_end_reason(
                reset_expected=True,
                saw_banner=False,
                saw_boot_load_ok=False,
                elapsed_s=10.0,
                ceiling_s=10.0,
            ),
        )


class ReadinessTransportStopTests(unittest.TestCase):
    """`!STOP_STATUS` silence is resolved by one idempotent MIDI Stop, then refused by name."""

    def _run(self, tmp: Path, *, replies_after_stop: bool):
        log = tmp / "attach.log"
        log.write_text("\n".join(f"line {i}" for i in range(30)), encoding="utf-8")
        state = {"stopped": False, "stop_calls": 0}

        class FakeSession:
            session_log = log

            def send_command(self, command, *, nonce, end_nonce):
                if not state["stopped"]:
                    # Transport-gated command: a running transport produces no reply at all.
                    return True, "accepted"
                with log.open("a", encoding="utf-8") as fh:
                    fh.write(f"#CAP,1,TRACE_FENCE,{nonce}\n")
                    fh.write("#STOP_STATUS,us=9,state=STOPPED,pending=0,phase=0,queue=0/4\n")
                    fh.write(f"#CAP,2,TRACE_FENCE,{end_nonce}\n")
                return True, "accepted"

            def fenced_response(self, nonce, end_nonce, *, timeout_s):
                if not state["stopped"]:
                    return None
                return ["#STOP_STATUS,us=9,state=STOPPED,pending=0,phase=0,queue=0/4"]

        def stop() -> None:
            state["stop_calls"] += 1
            if replies_after_stop:
                state["stopped"] = True

        fake = FakeSession()
        with patch.object(mc, "active_managed_capture", lambda: fake):
            ready, reason = mc.establish_attach_readiness(
                timeout_s=3.0, send_transport_stop=stop, stop_retries=1
            )
        return ready, reason, state

    def test_silent_probe_is_resolved_by_one_idempotent_stop(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            ready, reason, state = self._run(Path(tmp), replies_after_stop=True)
        self.assertTrue(ready, reason)
        self.assertEqual(1, state["stop_calls"], "the stop must be sent exactly once")
        self.assertIn("idempotent MIDI Stop", reason)

    def test_a_device_that_stays_silent_is_refused_by_name_without_resetting(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            ready, reason, state = self._run(Path(tmp), replies_after_stop=False)
        self.assertFalse(ready)
        self.assertEqual(1, state["stop_calls"])
        self.assertIn("not answering", reason)

    def test_readiness_without_a_stop_callback_is_unchanged(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "attach.log"
            log.write_text("\n".join(f"line {i}" for i in range(30)), encoding="utf-8")

            class SilentSession:
                session_log = log

                def send_command(self, command, *, nonce, end_nonce):
                    return True, "accepted"

                def fenced_response(self, nonce, end_nonce, *, timeout_s):
                    return None

            with patch.object(mc, "active_managed_capture", lambda: SilentSession()):
                ready, reason = mc.establish_attach_readiness(timeout_s=0.3)
        self.assertFalse(ready)
        self.assertIn("no fenced !STOP_STATUS reply", reason)


class BootBannerDetectionTests(unittest.TestCase):
    def test_banner_is_read_from_this_run_s_log(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            log = out / "session.log"
            log.write_text("[Memory] heap\n#CAP,1583004,DIAG,boot_reset,crash=0\n", encoding="utf-8")
            session = mc.ManagedCaptureSession(out_dir=out)
            session._session_log = log
            self.assertTrue(session.boot_banner_seen())

    def test_an_attaching_run_reports_no_banner(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            log = out / "session.log"
            log.write_text("line\nline\n", encoding="utf-8")
            session = mc.ManagedCaptureSession(out_dir=out)
            session._session_log = log
            self.assertFalse(session.boot_banner_seen())


if __name__ == "__main__":
    unittest.main()
