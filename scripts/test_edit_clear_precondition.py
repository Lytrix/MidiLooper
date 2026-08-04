#!/usr/bin/env python3
"""Unit tests for edit baseline clear precondition helpers."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.legacy_edit_baseline import (
    _can_skip_clear_for_record,
    _track_cleared_for_record,
)


class EditClearPreconditionTests(unittest.TestCase):
    def test_never_skip_clear_when_loop_slot_set(self) -> None:
        lines = ["#CAP,1,ST,Track,EMPTY,EMPTY"]
        self.assertFalse(_can_skip_clear_for_record(lines, loop_slot=2))

    def test_stopped_with_disp_notes_not_cleared(self) -> None:
        lines = [
            "#CAP,1,ST,Track,PLAYING,STOPPED",
            "#CAP,2,DISP,1,STOPPED,1536,9,9,9,9,1",
        ]
        self.assertFalse(_track_cleared_for_record(lines, loop_slot=2))

    def test_empty_is_cleared(self) -> None:
        lines = ["#CAP,1,ST,Track,EMPTY,EMPTY"]
        self.assertTrue(_track_cleared_for_record(lines, loop_slot=2))


if __name__ == "__main__":
    unittest.main()
