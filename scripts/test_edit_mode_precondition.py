#!/usr/bin/env python3
"""Unit tests for LOOP_EDIT record precondition."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from unittest.mock import MagicMock

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.edit_mode_precondition import (
    ensure_loop_edit_before_record,
    last_edit_session_kind,
)


class LoopEditPreconditionTests(unittest.TestCase):
    def test_last_edit_session_kind_from_tail(self) -> None:
        lines = [
            "Edit session: LOOP_EDIT",
            "Edit session: NOTE_EDIT",
        ]
        self.assertEqual(last_edit_session_kind(lines), "NOTE_EDIT")

    def test_skips_press_when_not_in_note_edit(self) -> None:
        out_port = MagicMock()
        collector = MagicMock()
        collector.snapshot.return_value = ["[StorageManager] idle"]
        self.assertTrue(
            ensure_loop_edit_before_record(
                out_port,
                collector,
                press_ms=120,
                phase_wait_ms=50,
            )
        )
        out_port.send.assert_not_called()

    def test_skip_flag(self) -> None:
        out_port = MagicMock()
        collector = MagicMock()
        collector.snapshot.return_value = ["Edit session: NOTE_EDIT"]
        self.assertTrue(
            ensure_loop_edit_before_record(
                out_port,
                collector,
                press_ms=120,
                phase_wait_ms=50,
                skip=True,
            )
        )
        out_port.send.assert_not_called()


if __name__ == "__main__":
    unittest.main()
