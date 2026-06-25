#!/usr/bin/env python3
"""Unit tests for two-overdub undo/redo serial verification."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.verify.two_overdub_undo_redo import verify_two_overdub_undo_redo


def _st_line(from_state: str, to_state: str) -> str:
    return f"#CAP,1,ST,Track,{from_state},{to_state},0"


class TwoOverdubUndoRedoSerialVerifyTests(unittest.TestCase):
    def test_ok_two_overdub_full_undo_to_empty_then_redo(self) -> None:
        lines = [
            _st_line("ARMED", "RECORDING"),
            _st_line("RECORDING", "STOPPED_RECORDING"),
            _st_line("STOPPED_RECORDING", "PLAYING"),
            _st_line("PLAYING", "OVERDUBBING"),
            _st_line("OVERDUBBING", "PLAYING"),
            _st_line("PLAYING", "OVERDUBBING"),
            _st_line("OVERDUBBING", "PLAYING"),
            "Overdub undone",
            "Overdub undone",
            "Overdub undone",
            "#CAP,10,DISP,0,PLAYING,1536,0,0,0,0,0",
            "Overdub redone",
            "Overdub redone",
            "Overdub redone",
            "#CAP,20,DISP,0,PLAYING,1536,96,32,32,96,1",
        ]
        args = SimpleNamespace(overdub_passes=2, undo_steps=3, redo_steps=3)
        result = verify_two_overdub_undo_redo(lines, args)
        self.assertTrue(result["ok"], result.get("issues"))

    def test_fail_when_display_not_empty_after_undo_chain(self) -> None:
        lines = [
            _st_line("PLAYING", "OVERDUBBING"),
            _st_line("OVERDUBBING", "PLAYING"),
            _st_line("PLAYING", "OVERDUBBING"),
            _st_line("OVERDUBBING", "PLAYING"),
            "Overdub undone",
            "Overdub undone",
            "Overdub undone",
            "#CAP,10,DISP,0,PLAYING,1536,96,32,32,96,1",
            "Overdub redone",
            "Overdub redone",
            "Overdub redone",
        ]
        args = SimpleNamespace(overdub_passes=2, undo_steps=3, redo_steps=3)
        result = verify_two_overdub_undo_redo(lines, args)
        self.assertFalse(result["ok"])
        self.assertTrue(any("display_not_empty_after_undo_chain" in issue for issue in result["issues"]))


if __name__ == "__main__":
    unittest.main()
