## Why

NOTE_EDIT currently reconstructs current editable note geometry from `EditSession.store`, `baselineMap`, focus fields, overlap scratch, display ordering, and live-store scans. Same-pitch overlap captures (`session_20260807_021939`, `session_20260807_021022`) show that this lets stale committed geometry affect later `noteId` edits and forces guard patches that prevent correct current-span overlap edits.

This change introduces a single NOTE_EDIT owner for current editable note state while preserving stable `NoteId` identity and keeping display order derived.

## What Changes

- Add **NoteEditCurrentState** as the `EditSession`-owned authority for current editable note rows keyed by `NoteId`.
- Make `EditSession.store` the canonical event projection of `NoteEditCurrentState`, not independent editable-state authority.
- Define projection as intentionally lossy: visible/added rows project to MIDI event pairs; hidden/deleted rows remain only in current state.
- Route NOTE_EDIT geometry readers and writers through current state in stages, starting with read-only build/verify and projection parity.
- Update commit generation to derive `editPass` rows from current state compared to committed baseline, with legacy store-diff builders retained temporarily for parity checks only.
- Update undo/redo to restore current state and projected store as one logical snapshot.
- Remove stopgap compatibility state after parity tests pass: `sessionMovedNoteSpans`, session-moved overlap skip guards, and live-store geometry authority in resolver/action builder/commit paths.
- Record the ownership transfer in `docs/DECISION_LOG.md` before firmware implementation.

Non-goals:

- No new top-level Manager.
- No return to `selectedIdx` as identity; `EditorSelection` remains `NoteId`-based.
- No jam, scene, arrangement capture, or D13 work.
- No SD format change in this change.

## Capabilities

### New Capabilities

- `note-edit-current-state`: Owns current editable NOTE_EDIT note state per `NoteId`, projection to `EditSession.store`, presence semantics, invariants, and migration gates.

### Modified Capabilities

- `edit-session-action-geometry`: Geometry resolution and action building must read current spans/presence from NoteEditCurrentState, not projected live-store event pairs as editable-state authority.
- `note-edit-modification-session`: NOTE_EDIT commit/publication changes from baseline vs final live store to committed baseline vs current state; `EditSession.store` becomes canonical event projection.
- `note-edit-session-undo`: Session undo/redo must snapshot and restore current state with projected store as one logical unit.
- `internal-heap-external-memory-routing`: NoteEditCurrentState row storage must follow NOTE_EDIT external-memory-first routing and avoid hot-path external pool walks.

## Impact

Affected code areas:

- `include/EditSession.h` — add current-state owner under existing `EditSession`.
- `include/NoteEditCurrentState.h`, `src/EditManager/NoteEditCurrentState.cpp` — new current-state row types and owner APIs.
- `src/EditManager/NoteEditSessionLifecycle.cpp` — build/clear current state and canonical projection.
- `src/EditManager/NoteGeometryResolver.cpp`, `src/EditManager/EditSessionInteraction.cpp`, `src/EditManager/ResolveConstrainedGeometry.cpp` — read current-state rows for scope, target spans, presence, and restore.
- `src/EditManager/EditSessionActionBuilder.cpp`, `src/EditManager/ApplyEditSessionActions.cpp` — compare and mutate current state, then project.
- `src/EditManager/NoteEditSessionCommit.cpp` — commit current-state diffs through existing `Loop::saveNoteEditPass`.
- `src/EditManager/NoteEditSessionUndo*.cpp` — restore current state and projected store together.
- `src/EditManager/NoteEditFocus*.cpp`, `src/EditManager/NoteEditDisplayProjection.cpp`, `src/EditManager/NoteEditSelection.cpp` — migrate readers away from projected event geometry authority.
- `src/Utils/NoteMovementUtils.cpp`, `src/EditStates/EditSelectNoteState.cpp`, `src/ControlSurface/FaderDependentSnapshot.cpp` — remove direct NOTE_EDIT projected-store writes/normalization after owner APIs exist.
- Native tests for current-state projection, reader/writer gates, repeated same-pitch moves, hidden/deleted/added rows, undo/redo, and commit parity.

Reference plan: `docs/Plans/note_edit_session_current_state_refinement.md`.
