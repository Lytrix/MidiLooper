#!/usr/bin/env python3
"""Unit tests for note-edit session-state serial verification helpers."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from host_midi_automation_edit_baseline import (
    SCOPED_EDIT_PASS_REDONE,
    SCOPED_EDIT_PASS_UNDONE,
    _verify_session_state_enter,
    _verify_session_undo_redo_routing,
    _verify_warmup_empty_nav_create,
)


class EditSessionStateSerialVerifyTests(unittest.TestCase):
    def test_enter_ok_select_without_cycle(self) -> None:
        lines = [
            "[1] MIDI Encoder: Short press - entered note edit mode",
            "[1] EditSelectNoteState::onEnter at tick 8",
            "[1] MIDI Encoder: Entered SELECT mode (bracket=8)",
            "[2] POSITION EDIT: Note moved from step 0 to 4",
        ]
        result = _verify_session_state_enter(lines)
        self.assertTrue(result["ok"])
        self.assertFalse(result["cycle_on_enter"])

    def test_enter_fails_when_cycle_before_select(self) -> None:
        lines = [
            "[1] NoteEditSession opened editPass=0",
            "[1] Entered EditStartNoteState",
            "[1] Note edit type cycled to kind=3",
            "[1] MIDI Encoder: Entered SELECT mode (bracket=8)",
        ]
        result = _verify_session_state_enter(lines)
        self.assertFalse(result["ok"])
        self.assertIn("session_state:cycle_on_enter_press", result["issues"])

    def test_session_undo_routing_in_and_post_exit(self) -> None:
        lines = [
            "[1] MIDI Encoder: Short press - entered note edit mode",
            "[2] MIDI: NoteEditSession undo",
            "[3] MIDI: NoteEditSession redo",
            "[4] MIDI Encoder: Long press - exited edit mode",
            f"[5] {SCOPED_EDIT_PASS_UNDONE} session=0 editPass=0 edits=6",
            f"[6] {SCOPED_EDIT_PASS_REDONE} session=0 editPass=0 edits=6",
        ]
        result = _verify_session_undo_redo_routing(lines)
        self.assertTrue(result["ok"])
        self.assertEqual(result["in_edit_undo"], 1)
        self.assertEqual(result["post_exit_redo"], 1)

    def test_session_undo_routing_accepts_legacy_post_exit_markers(self) -> None:
        lines = [
            "[1] MIDI Encoder: Short press - entered note edit mode",
            "[4] MIDI Encoder: Long press - exited edit mode",
            "[5] Note edit pass undone editPass=0 edits=6",
            "[6] Note edit pass redone editPass=0 edits=6",
        ]
        result = _verify_session_undo_redo_routing(lines)
        self.assertEqual(result["post_exit_undo"], 1)
        self.assertEqual(result["post_exit_redo"], 1)

    def test_warmup_empty_nav_creates_note(self) -> None:
        lines = [
            "[1] MIDI Encoder: Short press - entered note edit mode",
            "[2] MIDI Encoder: Entered SELECT mode (bracket=8)",
            "[3] Select fader: selected empty step at tick 48 (no note)",
            "[4] Note selection changed: 2 -> -1",
            "[5] Button press: Length Edit Mode (double)",
            "[6] NOTELEN double: create note at bracket",
            "[7] EditSelectNoteState: Created 32nd note",
        ]
        result = _verify_warmup_empty_nav_create(lines)
        self.assertTrue(result["ok"])
        self.assertEqual(result["notelen_action"], "create")

    def test_warmup_empty_nav_fails_on_stale_delete(self) -> None:
        lines = [
            "[1] MIDI Encoder: Short press - entered note edit mode",
            "[2] Select fader: selected empty step at tick 48 (no note)",
            "[3] Button press: Length Edit Mode (double)",
            "[4] NOTELEN double: delete selected note",
        ]
        result = _verify_warmup_empty_nav_create(lines)
        self.assertFalse(result["ok"])
        self.assertIn("warmup_create:notelen_action_delete", result["issues"])


if __name__ == "__main__":
    unittest.main()
