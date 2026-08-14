## Context

`StorageManager` is the only persistence owner (DEC-008). Stop/undo admits `LoopUndoHistory` and `stepDeferredSaveJobFooter` always enters `DeferredSaveStage::UndoStacks`, walking every track’s `GlobalUndoStack`. Evidence: [`112104`](../../../captures/session_20260813_112104.log), [`154823`](../../../captures/session_20260813_154823.log).

The loop slot file already stores content layers (`StorageLoopIo` pass headers, including `stateRaw`). `applyUndoEntry` (except `ClearSlot` / `LoopBoundaryChange`) toggles Active/Disabled on those layers. DEC-024 Phase 1 filters undo by slot; Phase 2 (move `GlobalUndoStack` Track → Loop) is **not** this change.

Architecture: [`docs/Plans/loop_layer_history_persistence_architecture.md`](../../../docs/Plans/loop_layer_history_persistence_architecture.md). Decision: [DEC-035](../../../docs/DECISION_LOG.md#dec-035-loop-persists-content-only).

Primary files: `src/StorageManager/DeferredSaveJobStages.cpp`, `src/Track/TrackCaptureStopCommit.cpp`, `src/TrackUndo.cpp`, `src/StorageManager/LoadLoopJob.cpp`, `LoopPasses` / `LoopCapture` / `LoopEditPasses`.

## Goals / Non-Goals

**Goals:**

- Prove content records contain operation boundaries and a prefix that defines the effective Loop.
- Reconstruct load-time editing state before deleting persisted undo.
- Delete `UndoStacks` so stop latency is no longer driven by undo-entry count.
- Keep `StorageManager` admission (`admit*` + `PersistenceWorkQueue`) and `handleMidiInput()` ordering unchanged.

**Non-Goals:**

- Replace `GlobalUndoStack` (Stage 3b).
- Clear-as-unlink, append-structured journal, checkpoint, range-first playback.
- Interval reservation, MIDI catch-up suppression, early USB / tier-0 restore.
- Full validate on record/overdub stop.

## Decisions

1. **Two derivations from content, not one.** Replay rebuilds current Loop state. Editing derivation groups records into undo units. A note-perfect replay without undo-unit boundaries fails Stage 1.
   - Alternative rejected: persist a slim undo index (`undo_TT_SS.bin`) — still O(entries) per stop and deleted at Stage 3.

2. **Stage 2 before Stage 3.** Load must answer tip, undo-step count, records per unit, redo empty, effective records, walk-to-empty, serialize/reload. `GlobalUndoStack` may be filled from that derivation on load until Stage 3b.
   - Alternative rejected: delete `UndoStacks` first and accept empty `U:` after reboot.

3. **Missing information is content metadata.** If Stage 1 cannot derive a boundary (DEC-031 companions, `noteEditPassIndex`, geometry), add an immutable field on the content record. Do not persist undo/redo. Do not keep `stateRaw` as hidden undo persist unless the prefix invariant fails — then name the missing content field.
   - Alternative rejected: restore persisted Active/Disabled as the long-term model.

4. **Reuse existing owners.** Extend `LoopPasses` / `LoadLoopJob` / `TrackUndo`. No new Manager, no `Source` type, no `PassStateChange`.

5. **Legacy bundle undo read** only if required to migrate existing cards. New writes do not emit `UndoStacks` after Stage 3.

## Risks / Trade-offs

- [Stage 1 finds a grouping gap] → add content metadata; do not invent undo persist; do not start Stage 3.
- [Filling GUS on load duplicates runtime state] → accepted until Stage 3b; single in-session owner stays `TrackUndo`.
- [Deleting UndoStacks does not make persist cheap] → accepted; new floor is remaining `LoopPersist`.
- [Chunk pool still bounds RAM-resident history] → persisted content ≠ resident content; later layers address this.

## Migration Plan

1. Native Stage 1 audit while still writing today’s bundle undo stack.
2. Stage 2 load derivation; reboot `U:nn` matches tip depth with UndoStacks still written.
3. Stage 3 stop writing/walking UndoStacks; keep read-for-migrate if cards require it.
4. Rollback: revert Stage 3 only; Stage 1–2 fixtures stay.

## Open Questions

- Stage 1 outcome: content prefix is sufficient for record/overdub/edit batches. Session `editPassIds` stay E:-only (no new undo-unit id). `LoopGeometry` is the named content record for `LoopBoundaryChange` (Stage 1b).
- After Stage 3, is a one-time legacy bundle-undo reader required for existing cards, or can load ignore that section?
