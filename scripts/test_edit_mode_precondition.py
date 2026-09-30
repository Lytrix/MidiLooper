#!/usr/bin/env python3
"""Unit tests for LOOP_EDIT record precondition.

Run: .venv/bin/python scripts/test_edit_mode_precondition.py
"""

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

_LOOP_ACK = "Edit session: LOOP_EDIT (Program 0, Note 100 trigger)"
_NOTE_ACK = "Edit session: NOTE_EDIT (Program 1, Note 0 trigger)"
_EXIT_ACK = "[INFO] MIDI Encoder: Long press - exited edit mode"


class _FakeCollector:
    """Minimal stand-in: the ack a press leaves arrives in the same line buffer."""

    def __init__(self, lines: list[str] | None = None) -> None:
        self.lines: list[str] = list(lines or [])

    def snapshot(self) -> list[str]:
        return list(self.lines)


class _FakePort:
    def __init__(self, collector: _FakeCollector, ack: str | None) -> None:
        self.collector = collector
        self.ack = ack
        self.notes: list[int] = []

    def send(self, message) -> None:
        if getattr(message, "type", None) != "note_on":
            return
        self.notes.append(message.note)
        if self.ack:
            self.collector.lines.append(self.ack)


class LoopEditPreconditionTests(unittest.TestCase):
    def test_last_edit_session_kind_from_tail(self) -> None:
        lines = [_LOOP_ACK, _NOTE_ACK]
        self.assertEqual(last_edit_session_kind(lines), "NOTE_EDIT")

    def test_loop_edit_ack_needs_no_gesture(self) -> None:
        collector = _FakeCollector(["[StorageManager] idle", _LOOP_ACK])
        port = _FakePort(collector, ack=None)
        self.assertTrue(
            ensure_loop_edit_before_record(
                port, collector, phase_wait_ms=50, timeout_s=0.01
            )
        )
        self.assertEqual(port.notes, [])

    def test_exit_row_alone_proves_loop_edit_without_a_press(self) -> None:
        collector = _FakeCollector([_EXIT_ACK])
        port = _FakePort(collector, ack=None)
        self.assertTrue(
            ensure_loop_edit_before_record(
                port, collector, phase_wait_ms=50, timeout_s=0.01
            )
        )
        self.assertEqual(port.notes, [])

    def test_skip_flag(self) -> None:
        collector = _FakeCollector([_NOTE_ACK])
        port = _FakePort(collector, ack=None)
        self.assertTrue(
            ensure_loop_edit_before_record(
                port, collector, phase_wait_ms=50, timeout_s=0.01, skip=True
            )
        )
        self.assertEqual(port.notes, [])

    def test_unproven_mode_is_proved_by_a_long_edit_press(self) -> None:
        collector = _FakeCollector(["boot: no edit session yet"])
        port = _FakePort(collector, ack=_LOOP_ACK)
        slept: list[float] = []
        with patch("hitl.edit_mode_precondition.time.sleep", side_effect=slept.append):
            self.assertTrue(
                ensure_loop_edit_before_record(
                    port,
                    collector,
                    phase_wait_ms=50,
                    exit_press_ms=700,
                    timeout_s=0.2,
                )
            )
        # Note 38 is the edit button; the exit action is the *long* press.
        self.assertEqual(port.notes, [38])
        self.assertIn(0.7, slept)

    def test_note_edit_is_left_by_the_long_press_ack(self) -> None:
        collector = _FakeCollector([_NOTE_ACK])
        port = _FakePort(collector, ack=_EXIT_ACK)
        with patch("hitl.edit_mode_precondition.time.sleep"):
            self.assertTrue(
                ensure_loop_edit_before_record(
                    port, collector, phase_wait_ms=50, timeout_s=0.2
                )
            )
        self.assertEqual(port.notes, [38])

    def test_press_without_an_ack_is_refused(self) -> None:
        collector = _FakeCollector(["boot: no edit session yet"])
        port = _FakePort(collector, ack=None)
        with patch("hitl.edit_mode_precondition.time.sleep"):
            self.assertFalse(
                ensure_loop_edit_before_record(
                    port,
                    collector,
                    phase_wait_ms=50,
                    timeout_s=0.01,
                )
            )
        # It pressed once (the proof is attempted, never assumed) and then refused.
        self.assertEqual(port.notes, [38])

    def test_loop_edit_is_never_inferred_from_note_edit(self) -> None:
        collector = _FakeCollector([_NOTE_ACK])
        port = _FakePort(collector, ack=_NOTE_ACK)
        with patch("hitl.edit_mode_precondition.time.sleep"):
            self.assertFalse(
                ensure_loop_edit_before_record(
                    port, collector, phase_wait_ms=50, timeout_s=0.01
                )
            )

    def test_ensure_note_edit_exited_uses_short_press(self) -> None:
        out_port = MagicMock()
        collector = MagicMock()
        collector.snapshot.side_effect = [
            [_NOTE_ACK],
            [_NOTE_ACK],
            [_NOTE_ACK, _LOOP_ACK],
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
