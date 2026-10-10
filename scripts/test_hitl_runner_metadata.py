#!/usr/bin/env python3
"""Ensure managed-run metadata reflects scenario options forwarded to the legacy leg."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.runner import _legacy_int_option


class LegacyManagedOptionTests(unittest.TestCase):
    def test_split_and_equals_forms_override_managed_metadata_defaults(self) -> None:
        self.assertEqual(23, _legacy_int_option(["--record-bars", "23"], "--record-bars", 2))
        self.assertEqual(6, _legacy_int_option(["--overdub-bars=6"], "--overdub-bars", 2))

    def test_last_repeated_value_matches_argparse_behavior(self) -> None:
        self.assertEqual(24, _legacy_int_option(
            ["--record-bars", "23", "--record-bars=24"], "--record-bars", 2
        ))

    def test_missing_or_unparseable_option_keeps_default(self) -> None:
        self.assertEqual(2, _legacy_int_option(["--midi-channel", "8"], "--record-bars", 2))
        self.assertEqual(2, _legacy_int_option(["--record-bars", "many"], "--record-bars", 2))


    def test_managed_capture_config_uses_forwarded_bar_options(self) -> None:
        from types import SimpleNamespace
        from unittest import mock
        from hitl import runner
        captured = {}
        def capture(config, _run_fn):
            captured["config"] = config
            return 0
        args = SimpleNamespace(preset="windowed_content_parity", build_env="teensy41-capture-serial-window-qualification")
        with mock.patch.object(runner, "_should_use_managed_capture", return_value=True):
            with mock.patch.object(runner, "require_slot_target", return_value=(8, 4, 8)):
                with mock.patch("hitl.managed_capture.run_with_managed_capture", side_effect=capture):
                    result = runner._run_with_optional_managed_capture(
                        args, ["--record-bars", "23", "--overdub-bars", "6"], ["windowed_content_parity"], lambda: 0
                    )
        self.assertEqual(0, result)
        self.assertEqual(23, captured["config"].record_bars)
        self.assertEqual(6, captured["config"].overdub_bars)
if __name__ == "__main__":
    unittest.main()
