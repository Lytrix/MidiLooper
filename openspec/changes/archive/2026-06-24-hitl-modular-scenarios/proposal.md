## Why

Record/overdub and note-edit HITL automation live in two monolithic Python scripts (~8k lines
combined). Operators cannot run focused integration tests (minimal edit smoke, edit+overdub during
note edit) without executing the full overlap suite. The edit+overdub-during-edit flow is specified
in firmware but lacks HITL coverage; session **E:** undo cannot remove an overdub pass without
exiting NOTE_EDIT.

## What Changes

- Introduce composable **scenarios** and a unified runner (`host_midi_hitl.py`) with backward-compatible
  **`base`** and **`edit_full`** presets.
- Add **`edit_overdub_during_note_edit`** scenario: record → overdub → edit pass → overdub in note
  edit → **E:** undo overdub → second edit pass → exit → global undo/redo.
- Extend **SessionUndoEntry** so overdub stop during note edit pushes an **E:** entry that disables
  the new **overdubPass** and rematerializes **NoteEditSession.store**.

## Non-goals

- Replacing `capture_session.py` or overlap Track C fixes inside **`edit_full`**
- CI wiring in this change (follow-up PR)

## Capabilities

### New Capabilities

- `hitl-automation` — composable HITL scenarios, presets, verify-only replay

### Modified Capabilities

- `note-edit-session-undo` — session undo for overdub during note edit

## Impact

- `scripts/hitl/`, `scripts/host_midi_hitl.py`
- Legacy wrappers: `host_midi_automation_baseline.py`, `host_midi_automation_edit_baseline.py`
- Firmware: `NoteEditSessionUndo`, `EditManager`, `TrackUndo`
- `.cursor/rules/HITL-*-Test-Flow.mdc`, `docs/Plans/hitl_modular_scenarios_enhancement.md`
