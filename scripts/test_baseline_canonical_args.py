"""Unit tests for canonical baseline arg injection."""

from __future__ import annotations

import unittest

from hitl.baseline_canonical_args import canonical_baseline_legacy_args, merge_legacy_cli_args


class CanonicalBaselineArgsTest(unittest.TestCase):
    def test_base_scenario_argv_includes_overdub_bars(self) -> None:
        from hitl.baseline_canonical_args import canonical_baseline_legacy_args, merge_legacy_cli_args

        legacy = merge_legacy_cli_args(canonical_baseline_legacy_args(), ["--track-number", "5"])
        self.assertEqual("2", legacy[legacy.index("--overdub-bars") + 1])

    def test_canonical_args_include_bar_sync_record_overdub(self) -> None:
        args = canonical_baseline_legacy_args()
        self.assertEqual("2", args[args.index("--record-bars") + 1])
        self.assertEqual("2", args[args.index("--overdub-bars") + 1])
        self.assertIn("--start-transport", args)
        self.assertIn("--no-fixed-grid-notes", args)

    def test_merge_puts_user_overrides_last(self) -> None:
        merged = merge_legacy_cli_args(
            canonical_baseline_legacy_args(),
            ["--track-number", "5", "--record-bars", "4"],
        )
        self.assertEqual(["--track-number", "5", "--record-bars", "4"], merged[-4:])


if __name__ == "__main__":
    unittest.main()
