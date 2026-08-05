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


if __name__ == "__main__":
    unittest.main()
