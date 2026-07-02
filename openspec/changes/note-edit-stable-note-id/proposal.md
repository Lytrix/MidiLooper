## Why

The note editor identifies notes by **geometry** (`NoteRef`: channel, pitch, startTick, endTick). Move, pitch, length, overlap restore, and wrap handling **change geometry**, forcing every consumer to rediscover the edited note. Selection and fader feedback also key off **ephemeral list indices** (`selectedNoteIdx`, `displayIdx`), which shift when the inventory rebuilds — causing false “selection changed” motor events.

Stable **`NoteId`** on note-on events decouples **identity** (immutable) from **geometry** (mutable) and fixes selection, edit replay, undo, and fader sync. This is a **design-session change** (ownership + state transitions), not a fader-feedback patch. It follows Phase A (displayIdx / fader selection refactor on the current branch) and ships as a separate OpenSpec from `note-edit-fader-feedback-regression`.

User-confirmed 2026-07-01. Brownfield: [`docs/DELIVERABLE_TRACKING.md`](../../docs/DELIVERABLE_TRACKING.md); archived M8 edit specs in [`openspec/specs/`](../../openspec/specs/).

## What Changes

- **`NoteId`** (`uint32_t`) on **note-on `MidiEvent` only** — canonical identity in `LoopEventStore` + passes
- **`Loop::allocateNoteId()`** — sole allocator; monotonic, never reused within a loop
- **`EditorSelection`** replaces `NoteEditSelection` — stores `NoteId`s + `trackId` + `loopId` + `bracketTick`; **no stored list index**
- **`EditPass.targetNoteId`** replaces `NoteRef target` for note rows — **BREAKING** SD v6 wire format
- **Resolve at mutation time** — `findNoteOnById`, `deleteNoteById`; `DisplayNote.noteId` derived in `reconstructNotes`
- **Assign guard rail** — `assignMissingNoteIds()` on note-edit session open for `noteId == 0` stragglers
- **Phase 0 hygiene** — `TrackId` / `NoteId` type aliases on public API surfaces (no behavior change)
- **Remove `NoteRef`** for note targeting after Phase B (dev wipe — no dual-read)
- **Follow-ups (separate slices):** Phase C record/overdub display merge in edit; Phase D `ControlChangeId`

## Capabilities

### New Capabilities

- `note-edit-stable-note-id`: NoteId allocation, EditorSelection, identity invariants, allocation regression contract, fader gate on `primaryNote`

### Modified Capabilities

- `timeline-passes`: Note editPass rows use `targetNoteId` instead of `NoteRef target`; SD v6 rejection of v5 note-target wire
- `storage-loop-io`: SD v6 — `PersistedLoopSnapshot.nextNoteId`, larger `MidiEvent` with `noteId`
- `note-edit-modification-session`: Overlap/focus keys and commit paths use `NoteId` instead of `NoteRef`
- `overlap-hidden-note-select`: Delete/select target by `NoteId` instead of `NoteRef`
- `note-edit-session-undo`: Session undo snapshots store `EditorSelection` with `NoteId`s

## Impact

- **Firmware:** `Loop.h/cpp`, `MidiEvent.h`, `EditPass.h`, `EditApply.cpp`, `EditManager.cpp`, `NoteEditManager.cpp`, `NoteEditFocus.*`, `NoteUtils.*`, `SelectNavigation.*`, `Track.cpp` (record append), `StorageLoopIo.cpp`
- **Tests:** New/extended `test_note_id_allocation` or `test_edit_apply`; update all native factories for `noteId`; HITL fader sweep after Phase A stable
- **Storage:** **BREAKING** v6 — dev wipe and re-record; reject v5 slot files with old `EditPass` target size
- **Docs:** Backup plan at [`docs/plans/note_edit_stable_note_id_enhancement.md`](../../docs/plans/note_edit_stable_note_id_enhancement.md); update `PROJECT_STATE` / `CURRENT_WORK` when registered

## Non-Goals

- Parallel `Note` object graph or sidecar id map
- `ControlChangeId` (Phase D — after note path stable)
- Record/overdub **display merge** during NOTE_EDIT (Phase C)
- Jam / multi-loop / persistence overlay changes
- SD migration from v5 — clean break only

## Open Decisions (TBD)

- **Phase A exit gate** — **Satisfied** (2026-07-02): HITL slow fader-1 sweep PASS — 59 nav slots, `select_ignored_rate=0`, sibling sync OK. Handoff: [`note_edit_stable_note_id_phase_a_handoff.md`](../../docs/plans/note_edit_stable_note_id_phase_a_handoff.md). Phase B starts only after D0a resolution + explicit user scope.
- **`EntityIds.h` scope and naming** — **Resolved (2026-07-02):** **(C) document-only** + post-Phase B co-location. Temporary hub during Phase B; after Phase B, move `NoteId` → `MidiEvent.h`, `TrackId` → `NoteEditSessionState.h`, delete `EntityIds.h`. PassId/LoopId/undo/storage ids stay domain-local.

## Delivery Sequence

| Phase | Work |
|-------|------|
| **0** | `TrackId` + `NoteId` type aliases (API surfaces only) |
| **A** | displayIdx refactor + fader selection (current branch — prerequisite) |
| **B** | NoteId schema + `EditorSelection` (this change) |
| **C** | Record/overdub in edit + display merge |
| **D** | `ControlChangeId` |
