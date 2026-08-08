# Preflight — note-edit-current-state

**Mode:** Full preflight (ownership-transfer formal trigger)  
**OpenSpec:** [`openspec/changes/note-edit-current-state/`](.)  
**Date:** 2026-08-07

---

## Problem

During NOTE_EDIT, current editable note geometry is reconstructed from `EditSession.store`, `baselineMap`, `NoteEditFocus`, overlap scratch, display ordering, and live-store scans. Same-pitch overlap captures (`session_20260807_021939`, `session_20260807_021022`) show stale committed baseline geometry affecting later `NoteId` edits, and stopgap guards (`sessionMovedNoteSpans`, overlap skip guards) prevent correct current-span overlap edits against already-moved notes.

User-visible behavior after this change: moving a second same-pitch note can modify the first moved note at its **current** span; hidden/deleted/added notes remain editable and committable by stable `NoteId` without event-pair reconstruction heuristics.

## Domain

Note edit — `EditManager` / `EditSession` / NOTE_EDIT geometry pipeline.

## Similar historical decisions

### Search locations

- [x] [`docs/DECISION_LOG.md`](../../docs/DECISION_LOG.md)
- [x] `docs/plans/` — [`note_edit_session_current_state_refinement.md`](../../docs/plans/note_edit_session_current_state_refinement.md)
- [x] Active OpenSpec — `openspec/changes/note-edit-current-state/`
- [x] `openspec/specs/edit-session-action-geometry/`, `note-edit-modification-session/`, `note-edit-session-undo/`

### Relevant findings

| ID / artifact | Relevance |
|---------------|-----------|
| **DEC-028** | EditSessionAction geometry pipeline — live store as geometry authority; overlap scratch until retired |
| **DEC-014** | Dual normalization boundaries — micro vs macro commit |
| **DEC-010** | Ownership evolution protocol — transfer required here |
| **DEC-007** | Historical decision reuse — extend owner vs new abstraction |
| Archived `edit-session-action-geometry` | Normative: `applyEditSessionActions` mutates live store; commit from baseline vs final live store |
| `note_edit_session_current_state_refinement.md` | Architecture verification and migration plan |

### Existing owner

`EditSession.store` (`sessionMidiEvents()` / `track.editAwareMidiEvents()`) plus scattered reconstruction: `baselineMap`, `NoteEditFocus.last`, `overlapNotes`, `changedOverlapNoteIds`, `sessionMovedNoteSpans`, live-store scans (`readLiveLinearSpan`, `liveStoreHasNotePair`).

### Existing extension point

`EditSession` session lifecycle, `applyEditSessionActions`, `buildEditSessionActions`, `NoteGeometryResolver::resolve`, session undo stack. No existing owner method covers current editable state per `NoteId` with presence semantics.

### Reuse possible

**NO** — extending `EditSession.store` or adding a derived cache beside it duplicates ownership. DEC-028 pipeline can be extended once current state is the mutation target; the new row collection is required for hidden/deleted rows absent from projected MIDI.

### If no — why not

A geometry index or live-store overlay would keep `EditSession.store` as implicit authority and continue reconstructing hidden/deleted/current meaning from event absence plus baseline heuristics — the failure mode confirmed in 021939/021022 captures.

### Architecture review required

**YES** — ownership transfer documented in OpenSpec design and this preflight; no separate reassessment doc required because transfer protocol fields are complete below.

---

## Loaded docs

- [x] `docs/runtime/PROJECT_STATE.md`
- [x] `docs/runtime/CURRENT_WORK.md` — updated to list this change as now implementing
- [x] `docs/DECISION_LOG.md` — DEC-028, DEC-010, DEC-014
- [x] `docs/00-authority/PROJECT_INTENT.md` (via ARCHITECTURE_RULES)
- [x] `docs/00-authority/ARCHITECTURE_RULES.md`
- [x] `docs/00-authority/DELIVERY_RULES.md`
- [x] `openspec/changes/note-edit-current-state/tasks.md`
- [x] `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`
- [x] `docs/Guides/INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md`
- [x] `docs/00-authority/NAMING.md`

## Extension point (implementation)

Extend `EditSession` with `NoteEditCurrentState` owner APIs; extend existing geometry pipeline stages to **read** current state and route **apply** through owner mutation + projection refresh. Do not add a new top-level Manager.

## Ownership change

| Field | Answer |
|-------|--------|
| Current owner | `EditSession.store` plus scattered reconstruction helpers (`baselineMap` for committed baseline only; focus/overlap scratch; live-store scans) |
| Target owner | `NoteEditCurrentState` inside `EditSession` |
| Transfer needed | **YES** |
| Compat removal trigger | Delete `sessionMovedNoteSpans`, session-moved overlap skip guards, live-store geometry authority in resolver/action builder/commit, and direct NOTE_EDIT writes through `track.editAwareMidiEvents()` after native fixtures (021939, 021022, hidden/deleted/added, undo/redo, commit parity) and user-approved HITL edit retest pass |

## Files affected

| Area | Paths (estimate) |
|------|------------------|
| Current-state owner | `include/NoteEditCurrentState.h`, `src/EditManager/NoteEditCurrentState.cpp`, `include/EditSession.h` |
| Lifecycle | `NoteEditSessionLifecycle.cpp` |
| Geometry pipeline | `NoteGeometryResolver.cpp`, `EditSessionInteraction.cpp`, `ResolveConstrainedGeometry.cpp`, `EditSessionActionBuilder.cpp`, `ApplyEditSessionActions.cpp` |
| Focus / display | `NoteEditFocus*.cpp`, `NoteEditDisplayProjection.cpp`, `NoteEditSelection.cpp` |
| Commit / undo | `NoteEditSessionCommit.cpp`, `NoteEditSessionUndo*.cpp` |
| Edit ops | `NoteMovementUtils.cpp`, `EditSelectNoteState.cpp`, `ControlSurface/FaderDependentSnapshot.cpp` |
| Tests | `test_note_edit_current_state/` (new), updates to existing edit-session suites |

## New abstractions

| Name | Kind | Justification |
|------|------|---------------|
| `NoteEditCurrentState` | Collection/owner on `EditSession` | Single `NoteId`-keyed authority for current editable note state |
| `NoteEditCurrentNoteState` | Row struct | One row per `NoteId`: committed span, current span, presence |
| `NoteEditPresenceType` | Enum | Explicit visibility/lifecycle: Visible, Hidden, Deleted, Added |

Reuse decision: **NO** — no existing structure owns presence + current span per `NoteId` during NOTE_EDIT.

## Persistence impact

**None** — firmware-only. No SD format bump. `Loop::saveNoteEditPass` publication path unchanged; commit **diff source** changes from projected store to current state (in-RAM only during session).

## Undo impact

**Session undo** — entries must restore `NoteEditCurrentState` and projected `EditSession.store` as one logical snapshot. Redo payloads derive from current state. Global undo stack unchanged during active NOTE_EDIT session undo.

## Migration required

**Firmware-only** with staged reader/writer migration and parity checks. Host native tests required at each phase. No SD migration.

## Architecture review required (summary)

**YES** — ownership transfer per DEC-010 and OpenSpec design. Alternatives rejected: derived cache beside store (duplicated ownership); return to `selectedIdx` identity (rejected in proposal).

## Authority conflict check

| Source | Conflicts with architecture or intent? |
|--------|--------------------------------------|
| OpenSpec / tasks | **NO** — explicit ownership transfer with migration and removal trigger |
| Proposed new class/helper | **NO** — owner stays inside `EditSession`; no new Manager |

---

## Context summary

- **Owner after transfer:** `NoteEditCurrentState` inside `EditSession`; `baselineMap` stays committed transaction baseline; `EditSession.store` becomes canonical lossy projection.
- **Formal trigger:** mutable ownership transfer for NOTE_EDIT editable geometry.
- **Active OpenSpec:** `note-edit-current-state` (this change).
- **Decision log:** DEC-029 appended; extends/supersedes live-store-as-authority aspects of archived `edit-session-action-geometry` via OpenSpec delta specs.
- **Identifiers confirmed:** `NoteEditCurrentState`, `NoteEditCurrentNoteState`, `NoteEditPresenceType`.
- **Hot-path constraint:** no external pool walks on fader/CC hot paths; current-state storage uses external-memory-first routing.
- **Tests:** native fixtures for 021939, 021022, repeated A/B moves, hidden/deleted/added, projection invariant, accessor gate, undo snapshot, commit parity; then `pio test -e native`, `teensy41-capture-serial` build, user-approved HITL edit retest.
- **Removal trigger:** `sessionMovedNoteSpans`, overlap skip guards, live-store geometry authority — after parity + HITL pass (tasks 7.3–7.5).

## Validation gates (confirmed)

| Gate | Scope |
|------|--------|
| Native tests | Current-state build/projection, reader/writer migration, undo/redo, commit parity |
| Full native | `pio test -e native` |
| Firmware build | `pio run -e teensy41-capture-serial` |
| HITL | User-approved upload + edit retest for same-pitch moved-note overlap |
| Compatibility removal | Tasks 7.3–7.5 after gates pass |
