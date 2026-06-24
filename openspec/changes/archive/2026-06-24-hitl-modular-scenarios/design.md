## Context

- [`host_midi_automation_baseline.py`](scripts/host_midi_automation_baseline.py) — record/overdub
- [`host_midi_automation_edit_baseline.py`](scripts/host_midi_automation_edit_baseline.py) — edit overlap suite (no overdub)
- [`m8-edit`](openspec/changes/m8-edit/specs/timeline-passes/spec.md) — overdub during note edit + three-step global undo

## Decisions

### D1 — Package layout

Shared helpers move to **`scripts/hitl/`** incrementally. Presets delegate to legacy scripts until
phase extraction completes; new scenarios import from legacy modules where needed.

### D2 — Scenario registry

Named scenarios compose **phases** (MIDI) and **verifiers** (serial). CLI: `--preset` or
`--scenarios id1,id2`.

### D3 — Backward compatibility

Default behavior of legacy entry points unchanged.

### D4 — Session undo overdub (firmware)

After overdub stop while **`isNoteEditActive()`**, push **`SessionUndoEntry`** with
**`overdubPassIdAtPush`** instead of clearing **`NoteEditSessionUndoStack`**.

**sessionUndo:** disable referenced **overdubPass**, rematerialize session, restore focus/selection.
**sessionRedo:** re-enable **overdubPass**, rematerialize.

Global **OverdubPassAdded** remains on **GlobalUndoStack**; session undo is a **view** undo during
edit. Post-exit global undo still reverses passes in stack order (m8 three-step scenario).

### D5 — Serial markers

Log **`EditSession overdub pass undone`** / **`EditSession overdub pass redone`** on session
overdub undo/redo paths.

## Verification

- `pio test -e native` — `test_note_edit_session_undo` overdub session entry
- HITL: `--scenarios edit_overdub_during_note_edit` with serial capture
