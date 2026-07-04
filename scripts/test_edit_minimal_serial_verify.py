#!/usr/bin/env python3
"""Unit tests for edit_minimal fixture identity serial verification."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.baseline_loop_inventory import record_layout_from_edit_fixture
from hitl.scenarios.edit_minimal import _verify_edit_minimal_fixture_identity


class EditMinimalSerialVerifyTests(unittest.TestCase):
    def setUp(self) -> None:
        self.layout = record_layout_from_edit_fixture({"record_bars": 2, "edit_record_fixture": True})

    def test_identity_ok_for_fixture_move_create_delete(self) -> None:
        lines = [
            "[1] Edit session: NOTE_EDIT",
            "[2] #DBG select_apply bracket_tick=0 note_idx=0 slot=0 prior_slot=-1 apply=1 reason=note_changed",
            "[3] POSITION EDIT: Note moved from step 0 to 4 (tick 0 -> 192, relative 0 -> 192)",
            "[4] Edit committed ChangeLength start=192 baselineEnd=288 newEnd=672",
            "[5] Select fader: selected empty step at tick 48 (no note)",
            "[6] Button press: Length Edit Mode (double)",
            "[7] NOTELEN double: create note at bracket",
            "[8] EditSelectNoteState: Created 32nd note (noteId=42, pitch=60, tick=48-72, length=24)",
            "[9] MIDI Encoder: Deleting note noteId=42 pitch=60, start=48, end=72",
            "[10] MIDI Encoder: Long press - exited edit mode",
        ]
        result = _verify_edit_minimal_fixture_identity(lines, layout=self.layout)
        self.assertTrue(result["ok"], result.get("issues"))

    def test_identity_fails_when_m0_deleted_at_origin(self) -> None:
        lines = [
            "[1] Edit session: NOTE_EDIT",
            "[2] Select fader: selected empty step at tick 48 (no note)",
            "[3] NOTELEN double: delete selected note",
            "[4] MIDI Encoder: Deleting note noteId=7 pitch=60, start=0, end=96",
        ]
        result = _verify_edit_minimal_fixture_identity(lines, layout=self.layout)
        self.assertFalse(result["ok"])
        self.assertTrue(any("wrong_delete_m0_at_origin" in issue for issue in result["issues"]))

    def test_delete_note_id_must_match_last_create(self) -> None:
        lines = [
            "[1] Edit session: NOTE_EDIT",
            "[2] Received pitchbend: ch=14 value=-6078",
            "[3] Received pitchbend: ch=14 value=-793",
            "[4] Select fader: selected empty step at tick 48 (no note)",
            "[5] Button press: Length Edit Mode (double)",
            "[6] NOTELEN double: create note at bracket",
            "[7] EditSelectNoteState: Created 32nd note (noteId=42, pitch=60, tick=48-72, length=24)",
            "[8] MIDI Encoder: Deleting note noteId=99 pitch=60, start=48, end=72",
        ]
        result = _verify_edit_minimal_fixture_identity(
            lines,
            layout=self.layout,
            markers=[
                "position_edit_requested",
                "length_edit_requested",
                "empty_step_tick=48",
            ],
        )
        self.assertFalse(result["ok"])
        self.assertIn("delete_note_id_mismatch:create=42 delete=99", result["issues"])


if __name__ == "__main__":
    unittest.main()
