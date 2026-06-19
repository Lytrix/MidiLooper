# Proposal — Note edit session consumer contract + overlap select/display/delete

**Change:** `overlap-hidden-note-select`  
**Kind:** architecture (symptoms: select / display / delete drift)  
**Status:** Proposed (2026-06-19)  
**Parent:** [note-edit-modification-session](../note-edit-modification-session/) (overlap owner **partial**; consumers and parallel state **not** consolidated)

## Why

Incremental spot fixes (delete pre-commit ordering, length-mode reset, hot-path commit guards) keep
closing individual HITL failures while the **session architecture** stays split:

- **Write path:** `focus.overlapNotes` (fader) vs `movingNote.deletedNotes` (encoder)
- **Read path:** `getCachedNotes()` for display, select, delete — no **overlapNotes** contract
- **Baseline path:** **baselineMap** from takes+edits replay vs parent doc “store scan” — undocumented
- **Commit path:** boundary events sprawl (select, delete, exit, ad-hoc materialize)

User-visible failures (HITL `20260619_212149` and manual repro in [BUG.md](./BUG.md)):

1. **Display** ≠ serial / store truth after valid edit passes
2. **Delete** wrong note (long mover); earlier moves reset
3. **Fader-1 select** bracket **hop** over **Hidden** inner overlap note

These are **projection and boundary** bugs, not overlap math bugs. [change-length-commit-rematerialize](../change-length-commit-rematerialize/) Track A/B remains **signed off**.

**Full analysis:** [architecture-review.md](./architecture-review.md) §10 retirement ladder.  
**Open decisions:** [CLARIFICATIONS.md](./CLARIFICATIONS.md) — resolve **C1–C6** before Phase 1 code.

## What Changes

### Architecture (primary)

- **`filterSelectableDisplayNotes`** (D1) — one filtered **DisplayNote** list for NOTE_EDIT select, display, delete; **NoteRef** indexing
- **Consumer contract:** NOTE_EDIT display, select, delete, bracket — **no** direct `getCachedNotes()`
- **Commit boundary state machine:** PREVIEW (fader only) → PRE-COMMIT RESOLVE → REMATERIALIZE; delete
  captures **deleteTarget** **NoteRef** before commit (see architecture-review §3.4)
- **Baseline policy:** document normative split — **baselineMap** from committed materialization;
  **focus.last** from live store (sync parent design via `/opsx:sync`)

### Implementation slices (secondary)

- D1–D5 from [design.md](./design.md) (`filterSelectableDisplayNotes`; select; display; delete)
- Phase 2 defers to parent **task 8.1** — retire **movingNote.deletedNotes** / encoder duplicate engine

## Capabilities

### New Capabilities

- `overlap-hidden-note-select`: **Hidden** overlap notes excluded from select navigation and display; delete targets fader-selected **NoteRef**; NOTE_EDIT uses `filterSelectableDisplayNotes`

### Modified Capabilities

- Parent [note-edit-modification-session/spec.md](../note-edit-modification-session/specs/note-edit-modification-session/spec.md) — add consumer + baseline-source requirements via delta in this change

## Impact

- **Firmware:** `NoteEditFocus.*` (`filterSelectableDisplayNotes`), `NoteEditManager.cpp`, `DisplayManager.cpp`, `SelectNavigation.cpp`, `EditManager.cpp` (boundary policy)
- **Tests:** `test_select_navigation`, `test_note_edit_focus`, HITL edit baseline AC1–AC5
- **Docs:** [architecture-review.md](./architecture-review.md); parent design baseline clarification on sync
- **Debt:** mark interim spot fixes; no new hot-path commits

## Non-Goals

- Overlap shorten/classify algorithm changes in `NoteMovementUtils` (except shared view helper)
- Track C insert/reorder ([note-edit-modification-session/tasks.md](../note-edit-modification-session/tasks.md) §7.4)
- Take / SD storage layout
- CRDT, full event sourcing, or new domain nouns without approval

## Open Decisions

All resolved — see [CLARIFICATIONS.md](./CLARIFICATIONS.md) (locked 2026-06-19).
