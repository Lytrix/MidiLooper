# Decision review — loop-content-history-persistence

**Date:** 2026-08-14  
**Change:** Layer A only (Stages 1–3)

## Historical review

### Searched

- [x] [`docs/DECISION_LOG.md`](../../../docs/DECISION_LOG.md)
- [x] Active OpenSpec — this change; `set-revision-persistence`; `workspace-session-persistence`
- [x] `openspec/specs/` — `timeline-passes`, `long-record-memory-headroom`, `note-edit-session-undo`, `undo-memory-trim`, `deferred-job-scheduler`
- [x] `docs/Plans/` — architecture plan, dump bugfix, DEC-024 Phase 2 handoff, persist work-queue B6

**Search terms:** `UndoStacks`, `LoopUndoHistory`, `GlobalUndoStack`, `stateRaw`, `admitLoopUndoHistory`, DEC-024, DEC-020, DEC-022, DEC-026, DEC-031, `undo_TT_SS`

### Relevant findings

- DEC-008, DEC-020, DEC-022 — keep persist owner, queue, bundle tail integrity
- DEC-024 Phase 2 — move GUS Track → Loop; **not** this change; Stage 3b will replace GUS
- DEC-026 / `lazy-slot-hydration` — COMMITTED as play gate; Layer D conflict later
- DEC-031 — companion ids on `UndoEntry`; Stage 1 content-metadata question
- Withdrawn: scoped `undo_TT_SS.bin`

### Existing reusable pattern

`LoopPasses` merge/materialize; `LoadLoopJob`; `TrackUndo::applyUndoEntry` disable/enable; `admit*` + `PersistenceWorkQueue`.

### Reuse decision

**YES**

### If NO

Not applicable.

## Reviewer gate

Reuse is YES. No new Manager. Persist payload is deleted after load-time editing state exists. Stage 3b is a separate DEC.
