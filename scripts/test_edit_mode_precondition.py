#!/usr/bin/env python3
"""Unit tests for LOOP_EDIT record precondition."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.edit_mode_precondition import (
    ensure_loop_edit_before_record,
    last_edit_session_kind,
)
from hitl.scenarios.edit_minimal import _ensure_note_edit_exited


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

    def test_exits_note_edit_with_short_press(self) -> None:
        out_port = MagicMock()
        collector = MagicMock()
        calls = {"n": 0}

        def _snapshot() -> list[str]:
            calls["n"] += 1
            if calls["n"] <= 2:
                return ["Edit session: NOTE_EDIT"]
            return ["Edit session: NOTE_EDIT", "Edit session: LOOP_EDIT"]

        collector.snapshot.side_effect = _snapshot
        with patch("hitl.edit_mode_precondition.time.sleep"), patch(
            "hitl.recovery.time.sleep"
        ):
            self.assertTrue(
                ensure_loop_edit_before_record(
                    out_port,
                    collector,
                    press_ms=120,
                    phase_wait_ms=50,
                    timeout_s=0.2,
                )
            )
        sent_notes = [
            call.args[0].note
            for call in out_port.send.call_args_list
            if getattr(call.args[0], "type", None) == "note_on"
        ]
        self.assertGreaterEqual(len(sent_notes), 1)
        self.assertEqual(sent_notes[0], 38)

    def test_unconfirmed_note_edit_exit_aborts(self) -> None:
        out_port = MagicMock()
        collector = MagicMock()
        collector.snapshot.return_value = ["Edit session: NOTE_EDIT"]
        with patch("hitl.edit_mode_precondition.time.sleep"), patch(
            "hitl.recovery.time.sleep"
        ):
            self.assertFalse(
                ensure_loop_edit_before_record(
                    out_port,
                    collector,
                    press_ms=120,
                    phase_wait_ms=50,
                    timeout_s=0.01,
                )
            )

    def test_ensure_note_edit_exited_uses_short_press(self) -> None:
        out_port = MagicMock()
        collector = MagicMock()
        collector.snapshot.side_effect = [
            ["Edit session: NOTE_EDIT"],
            ["Edit session: NOTE_EDIT"],
            ["Edit session: NOTE_EDIT", "Edit session: LOOP_EDIT"],
        ]
        with patch("hitl.scenarios.edit_minimal.time.sleep"):
            self.assertTrue(
                _ensure_note_edit_exited(
                    out_port,
                    collector,
                    press_ms=120,
                    phase_wait_ms=50,
                    timeout_s=0.01,
                    log_prefix="[test]",
                )
            )
        sent_notes = [
            call.args[0].note
            for call in out_port.send.call_args_list
            if getattr(call.args[0], "type", None) == "note_on"
        ]
        self.assertGreaterEqual(len(sent_notes), 1)
        self.assertEqual(sent_notes[0], 38)


if __name__ == "__main__":
    unittest.main()
