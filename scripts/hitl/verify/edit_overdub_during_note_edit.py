"""Serial verification for edit + overdub during note edit integration."""

from __future__ import annotations

import re
from typing import Optional


def verify_edit_overdub_during_note_edit(lines: list[str], args: object) -> dict[str, object]:
    from host_midi_automation_baseline import _count_capture_transitions
    from host_midi_automation_edit_baseline import SCOPED_EDIT_PASS_UNDONE

    transitions = _count_capture_transitions(lines)
    issues: list[str] = []

    playing_overdub = transitions.get(("PLAYING", "OVERDUBBING"), 0)
    overdub_playing = transitions.get(("OVERDUBBING", "PLAYING"), 0)
    if playing_overdub < 2:
        issues.append(f"need_playing_overdubbing_x2 got={playing_overdub}")
    if overdub_playing < 2:
        issues.append(f"need_overdubbing_playing_x2 got={overdub_playing}")

    note_edit_closed = sum(1 for line in lines if "NoteEditPassClosed" in line or "Note edit pass closed" in line)
    if note_edit_closed < 1:
        issues.append("missing_note_edit_pass_closed")

    overdub_added = sum(
        1 for line in lines if "OverdubPassAdded" in line or "Overdub pass added" in line
    )

    in_edit_undo = getattr(args, "in_edit_undo_after_overdub", True)
    if in_edit_undo:
        session_undo = any("EditSession undo" in line for line in lines)
        session_redo = any("EditSession redo" in line for line in lines)
        if not session_undo:
            issues.append("missing_edit_session_undo")
        if not session_redo:
            issues.append("missing_edit_session_redo")

    if getattr(args, "post_exit_global_undo_redo", True):
        scoped_undo = sum(1 for line in lines if SCOPED_EDIT_PASS_UNDONE in line)
        if scoped_undo < 1:
            issues.append("missing_post_exit_scoped_edit_pass_undo")

    enter_edit_idx = next(
        (i for i, line in enumerate(lines) if "entered note edit mode" in line), None
    )
    exit_edit_idx = next((i for i, line in enumerate(lines) if "exited edit mode" in line), None)
    if enter_edit_idx is not None and exit_edit_idx is not None:
        in_edit_overdub_added = sum(
            1
            for line in lines[enter_edit_idx:exit_edit_idx]
            if "OverdubPassAdded" in line or "Overdub pass added" in line
        )
        if in_edit_overdub_added > 0:
            issues.append(f"unexpected_in_edit_overdub_pass_added count={in_edit_overdub_added}")

    enter_edit = any("entered note edit mode" in line for line in lines)
    exit_edit = any("exited edit mode" in line for line in lines)
    if not enter_edit:
        issues.append("missing_enter_note_edit")
    if not exit_edit:
        issues.append("missing_exit_edit")

    move_edits = sum(1 for line in lines if "POSITION EDIT:" in line)
    length_edits = sum(1 for line in lines if "LENGTH EDIT:" in line)
    if move_edits < 1:
        issues.append("missing_position_edit")
    if length_edits < 1:
        issues.append("missing_length_edit")

    return {
        "ok": not issues,
        "issues": issues,
        "transitions": {f"{a}->{b}": c for (a, b), c in transitions.items()},
        "overdub_pass_added_count": overdub_added,
        "note_edit_pass_closed_count": note_edit_closed,
        "expected_pitch_bands": {
            "pre_edit_overdub": {"low": 24, "high": 39, "label": "C1-D#2"},
            "in_edit_overdub": {"low": 12, "high": 35, "label": "C0-B1"},
            "session_undo_targets": "in_edit_overdub_only",
        },
    }
