#!/usr/bin/env python3
"""Unit tests for legacy CLI arg collapse."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.baseline_canonical_args import collapse_legacy_cli_args
from hitl.legacy_edit_baseline import parse_edit_baseline_args
from hitl.scenarios.edit_full import _parse_edit_args


class CollapseLegacyCliArgsTests(unittest.TestCase):
    def test_last_loop_slot_wins(self) -> None:
        legacy = [
            "--loop-slot",
            "1",
            "--loop-slot",
            "2",
            "--loop-slot",
            "2",
        ]
        self.assertEqual(
            collapse_legacy_cli_args(legacy),
            ["--loop-slot", "2"],
        )

    def test_parse_edit_args_accepts_triple_loop_slot(self) -> None:
        args = type(
            "Args",
            (),
            {
                "legacy_args": [
                    "--track-number",
                    "5",
                    "--loop-slot",
                    "2",
                    "--loop-slot",
                    "2",
                    "--loop-slot",
                    "2",
                    "--midi-channel",
                    "5",
                    "--start-transport",
                ],
                "out_dir": Path("captures"),
                "verify_serial_log": None,
                "hitl_context": None,
            },
        )()
        parsed = _parse_edit_args(args)
        self.assertEqual(parsed.loop_slot, 2)
        self.assertEqual(parsed.track_number, 5)


if __name__ == "__main__":
    unittest.main()
