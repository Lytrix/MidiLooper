# Preflight — loop-content-history-persistence (Layer A)

**Mode:** Full — formal triggers fired (persist model, schema, undo semantics).  
**Date:** 2026-08-14  
**Decision:** [DEC-035](../../../docs/DECISION_LOG.md#dec-035-loop-persists-content-only)

## Problem

Overdub stop persists `UndoStacks` / `LoopUndoHistory`, stalling MIDI for seconds. After Layer A, the Loop persists content only; load reconstructs editing state; the duplicate persist payload is deleted.

## Domain

Persistence (Current workspace / Sets / SD). Adjacent: undo routing.

## Similar historical decisions

### Search locations

- [x] [`docs/DECISION_LOG.md`](../../../docs/DECISION_LOG.md)
- [x] `docs/Plans/` — `loop_layer_history_persistence_architecture.md`, `overdub_stop_playing_midi_dump_bugfix.md`, `loop_undo_ownership_phase2_handoff.md`, `current_set_persist_work_item_queue_enhancement.md`
- [x] Active OpenSpec — this change; `set-revision-persistence`, `workspace-session-persistence`, `lazy-slot-hydration` (Layer D conflict only)

### Relevant findings

- DEC-008 — `StorageManager` sole persist owner (reuse)
- DEC-020 — continuous runtime persistence; queue/admission (reuse)
- DEC-022 — runtime bundle tail integrity (do not regress)
- DEC-024 — Phase 1 filter shipped; Phase 2 move GUS Track → Loop is **not** this change
- DEC-026 — slot-level lazy COMMITTED = playable; Layer D publication conflict later
- DEC-031 — companion `editPassIds` on `UndoEntry` today; Stage 1 must show grouping in content
- `long-record-memory-headroom` currently requires an undo-stack persist stage — this change modifies that

### Existing owner

`StorageManager` persist; `Loop` / `LoopPasses` content; `TrackUndo` in-session undo.

### Existing extension point

`LoadLoopJob` load path; `LoopPasses` replay; `TrackUndo::undoDepthForLoop` / `applyUndoEntry`.

### Reuse possible

**YES**

### Architecture review required

**YES** — see architecture plan § Architecture gate (Layer A). Ownership of the persist payload is deleted, not moved to a new module. Stage 3b GUS replacement is a later DEC.

## Loaded docs

- [x] `docs/Runtime/PROJECT_STATE.md`
- [x] `docs/Runtime/CURRENT_WORK.md`
- [x] `docs/DECISION_LOG.md`
- [x] `docs/Authority/PROJECT_INTENT.md` (intent: persistence track)
- [x] `docs/Authority/ARCHITECTURE_RULES.md`
- [x] `docs/Authority/DELIVERY_RULES.md`
- [x] `openspec/changes/loop-content-history-persistence/tasks.md`
- Domain: `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`, `docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md`, `docs/Authority/NAMING.md`

## Ownership change

| Field | Answer |
|-------|--------|
| Current owner | `StorageManager` writes UndoStacks; `TrackUndo` owns GUS |
| Target owner | `StorageManager` writes Loop content only; `TrackUndo` still owns GUS until 3b |
| Transfer needed | NO for Layer A runtime undo; YES for persist payload deletion (payload removed, not transferred) |
| If YES — compat removal trigger | Stage 3: no `admitLoopUndoHistory`; legacy bundle-undo reader removed when cards no longer need it |

## Files affected

`DeferredSaveJobStages.cpp`, `TrackCaptureStopCommit.cpp`, `TrackUndo.cpp`, `LoadLoopJob.cpp`, `LoopPasses` / load helpers, native tests. Display sidebar after Stage 2.

## New abstractions

| Name | Kind | Justification |
|------|------|---------------|
| none | | Extend existing owners |

## Persistence impact

Remove runtime-bundle undo stage. Loop file remains content records. `stateRaw` removal is a Stage 1 proof, not assumed.

## Undo impact

In-session capture-pass / `NoteEditPassClosed` unchanged. Session `E:` unchanged. After Stage 2, reboot depth is derived from content.

## Migration required

Firmware + native tests. Existing cards: new firmware still loads them. Old firmware is not a reader of new files.

## Architecture review required (summary)

**YES** — [`loop_layer_history_persistence_architecture.md`](../../../docs/Plans/loop_layer_history_persistence_architecture.md)

## Authority conflict check

| Source | Conflicts with architecture or intent? |
|--------|--------------------------------------|
| OpenSpec / tasks | NO for Layer A. YES for Layer D vs `lazy-slot-hydration` — out of this change |
| Proposed new class/helper | NO |

## Context summary

- Owner: `StorageManager` persist; `Loop` content; `TrackUndo` until 3b
- Constraint: Stage 2 before Stage 3; no new Manager; no Source noun
- Active OpenSpec: this change (Layer A only)
- DEC-035 accepted; DEC-024 Phase 2 not executed
- Tests: `pio test -e native`; Stage 3 device on confirmation
- Do not mix Stage 7 / interval reservation / RC-J
