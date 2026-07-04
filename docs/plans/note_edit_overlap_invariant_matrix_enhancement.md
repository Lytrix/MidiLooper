# Handoff — NOTE_EDIT overlap invariant matrix

**Status:** **Superseded** (2026-07-04) by [`note_edit_session_action_geometry_enhancement.md`](note_edit_session_action_geometry_enhancement.md) and OpenSpec [`edit-session-action-geometry`](../../openspec/changes/edit-session-action-geometry/).

The incremental **`NoteOverlapEngine`** / live-path invariant expansion approach is retired in favor of the **`EditSessionAction`** declarative pipeline (analyze → build → apply).

**Keep:** macro/micro **`LoopEventValidation`** gates already shipped in `EditManager` / `NoteEditManager` and native tests in `test_note_edit_focus`.
