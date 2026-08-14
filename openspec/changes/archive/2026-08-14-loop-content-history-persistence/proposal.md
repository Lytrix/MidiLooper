## Why

Overdub stop while PLAYING stalls MIDI for seconds because `DeferredSaveStage::UndoStacks` re-serializes `GlobalUndoStack` (~1300 slices). The loop file already stores the content those entries describe. Persistence must stop owning undo/redo.

## What Changes

- The Loop persists **content records only**. Undo/redo is runtime/editor behavior derived from ordered content plus grouping rules (DEC-035).
- Stage 1 proves every existing `UndoEntry` operation boundary can be derived from content (or names missing **content** metadata).
- Stage 2 reconstructs load-time editing state (tip, undo units, depth, redo empty, effective records) while `GlobalUndoStack` remains in-session authority.
- Stage 3 **BREAKING** for the runtime-bundle wire: delete `UndoStacks` / `LoopUndoHistory` / `admitLoopUndoHistory`. New firmware still loads existing cards; old firmware is not a reader of new files.
- Display `U:nn` after reboot is derived from content, not restored from a saved count.

**Non-goals (this change):** replace `GlobalUndoStack` (Stage 3b, later DEC); clear-as-unlink / `lastUnlinkedSlotLink` (Layer B); append-structured journal (Layer B); checkpoint + tail (Layer C); range-first load / `isRangeAvailable` as play gate (Layer D); interval reservation; early USB / tier-0 restore; NOTE_EDIT `E:` session undo.

## Capabilities

### New Capabilities

- `loop-content-history`: Loop content records are the only durable history; load reconstructs current Loop and editing semantics; undo/redo is not persisted.

### Modified Capabilities

- `long-record-memory-headroom`: deferred save remains slice-bounded, but MUST NOT require an undo-stack persist stage.

## Impact

- `StorageManager` persist path: `stepDeferredSaveJobUndoStacks`, `isRuntimeBundleWorkType`, `admitLoopUndoHistory` (Track capture-stop + `TrackUndo`).
- `Loop` / `LoopPasses` / `LoadLoopJob`: content replay and load-time editing derivation. No new Manager.
- `TrackUndo` / `GlobalUndoStack`: in-session owner until Stage 3b. Display `getDisplayUndoCount` reads derived depth after Stage 2 load.
- Brownfield: [`docs/DELIVERABLE_TRACKING.md`](../../../docs/DELIVERABLE_TRACKING.md), [`docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../../../docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md), [`docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md`](../../../docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md), [`openspec/specs/timeline-passes/`](../../specs/timeline-passes/), [`openspec/specs/note-edit-session-undo/`](../../specs/note-edit-session-undo/).
- Scope: [`docs/Runtime/CURRENT_WORK.md`](../../../docs/Runtime/CURRENT_WORK.md). Task [#33](https://github.com/Lytrix/MidiLooper/issues/33); Bug [#32](https://github.com/Lytrix/MidiLooper/issues/32).
