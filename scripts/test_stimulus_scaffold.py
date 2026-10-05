#!/usr/bin/env python3
"""Unit tests for `stimulus scaffold` (host-only; writes under a temp dir, registers nothing)."""

from __future__ import annotations

import argparse
import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.stimuli import cli, scaffold
from hitl.stimuli.registry import get_stimulus

PARENT = "STIM-BASE-2-ONE-OD-01"


class CatalogueTests(unittest.TestCase):
    def test_gesture_catalogue_lists_real_helpers_without_ctx_argument(self) -> None:
        names = {sig.split("(")[0] for _m, sig, _d in scaffold.gesture_catalogue()}
        self.assertTrue({"press_record", "press_slot_overdub", "await_markers"} <= names)
        self.assertFalse(any("action_ctx" in sig for _m, sig, _d in scaffold.gesture_catalogue()))

    def test_control_constants_include_the_button_notes(self) -> None:
        constants = dict(scaffold.control_constants())
        self.assertEqual(constants["RECORD_BUTTON_NOTE"], 36)
        self.assertEqual(constants["CONTROL_CHANNEL_1BASED"], 16)


class ScaffoldTests(unittest.TestCase):
    def test_writes_temp_sidecar_and_recipe_with_overrides_and_leaves_parent_alone(self) -> None:
        parent_before = get_stimulus(PARENT)
        with tempfile.TemporaryDirectory() as tmp:
            result = scaffold.scaffold(PARENT, "T1", out_dir=Path(tmp), reason="need 3 bars", bars=3,
                                       must_prove=["x is true"], markers=["DIAG,post_restore_census,"])
            self.assertTrue(result.sidecar.is_file())
            self.assertTrue(result.recipe.is_file())
            self.assertEqual(result.spec.stimulus_id, f"{PARENT}-TEMP-T1")
            self.assertEqual(result.spec.base_bars, 3)
            self.assertEqual(result.spec.must_prove, ("x is true",))
            recipe = result.recipe.read_text(encoding="utf-8")
            self.assertIn("need 3 bars", recipe)
            self.assertIn("press_record", recipe)
            self.assertIn("| `DIAG,post_restore_census,` | OK |", recipe)
            self.assertTrue(result.markers_ok)
        self.assertEqual(get_stimulus(PARENT), parent_before)
        with self.assertRaises(KeyError):
            get_stimulus(f"{PARENT}-TEMP-T1")

    def test_unknown_marker_is_reported_not_raised(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            result = scaffold.scaffold(PARENT, "T2", out_dir=Path(tmp), markers=["DIAG,zz_nope_zz,"])
            self.assertFalse(result.markers_ok)
            self.assertIn("NOT YET", result.recipe.read_text(encoding="utf-8"))

    def test_rejects_bad_suffix_bars_and_unknown_parent(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(ValueError):
                scaffold.scaffold(PARENT, "bad suffix", out_dir=Path(tmp))
            with self.assertRaises(ValueError):
                scaffold.scaffold(PARENT, "T3", out_dir=Path(tmp), bars=0)
            with self.assertRaises(KeyError):
                scaffold.scaffold("STIM-NOPE-01", "T3", out_dir=Path(tmp))


class CliTests(unittest.TestCase):
    def _run(self, *argv: str) -> tuple[int, str]:
        parser = argparse.ArgumentParser()
        cli.add_stimulus_subparser(parser.add_subparsers(dest="command"))
        args = parser.parse_args(["stimulus", *argv])
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = cli.run_stimulus_command(args)
        return code, out.getvalue()

    def test_cli_requires_reason_and_honours_require_markers(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
                self._run("scaffold", PARENT, "C1", "--out-dir", tmp)
            code, text = self._run("scaffold", PARENT, "C1", "--reason", "r", "--marker", "DIAG,zz_nope_zz,",
                                   "--out-dir", tmp)
            self.assertEqual(code, 0)
            self.assertIn("not registered", text)
            code, text = self._run("scaffold", PARENT, "C2", "--reason", "r", "--marker", "DIAG,zz_nope_zz,",
                                   "--require-markers", "--out-dir", tmp)
            self.assertEqual(code, 1)
            self.assertIn("do not flash", text)

    def test_cli_unknown_parent_exits_2(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            code, _ = self._run("scaffold", "STIM-NOPE-01", "C3", "--reason", "r", "--out-dir", tmp)
            self.assertEqual(code, 2)


if __name__ == "__main__":
    unittest.main()
