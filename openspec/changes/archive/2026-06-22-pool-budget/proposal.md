## Why

Fixed pass and undo caps (`MAX_UNDO_HISTORY = 25`, `MAX_CAPTURE_PASSES_PER_LOOP = 25`) block
workloads that fit in available PSRAM/heap (e.g. 128-bar record + many small overdubs, or 99 undo
steps when pass undos are lightweight). Meanwhile disabled **capturePass** rows retain chunk refs and
disabled **editPass** rows stay in RAM — so fixed counts do not reliably protect memory.

Post-M8 **pool/playback hardening** ([`docs/DELIVERABLE_TRACKING.md`](../../../docs/DELIVERABLE_TRACKING.md))
calls for memory-driven admission and reclaim instead of arbitrary cardinality limits.

## What Changes

- **Remove fixed pass-count gates** on capture seal and edit save; admit when chunk pool / heap
  headroom allows (with reserves).
- **Chunk pool admission:** O(1) `usedChunkCount` / `freeChunkCount` / `canAllocChunkWithReserve`;
  `sealCapture` fails with **`PoolExhausted`** (replaces **`AtPassCap`**) when alloc cannot succeed
  after reclaim retry.
- **Heap admission for editPass:** `saveNoteEditPass` checks heap reserve + estimated **EditChange**
  cost; returns **`kInvalidEditPassId`** when admission fails (no fixed row count).
- **Undo depth from memory:** replace `trimGlobalUndoHistory` fixed 25 with pressure-based trim
  targeting **`PREFERRED_UNDO_DEPTH` (99)** when affordable.
- **Reclaim disabled passes:** free chunk refs and remove unreferenced **Disabled** capture/edit
  pass rows; release **ClearSlot** snapshot payloads when undo entries are trimmed.
- **Orchestration:** `TrackManager::reclaimUnreferencedDisabledPasses()` + idle hook in `main.cpp`;
  one retry on seal/edit commit after reclaim.
- **Session `E:` undo snapshots:** replace **`cloneShared`** full-store copies with **`EditChangeList`**
  + **`NoteEditFocus`** entries; **`sessionUndo`** / **`sessionRedo`** rebuild via
  **`rematerializeEditView`** + **`applyEditChangeList`** (task group 9).
- **Native tests** for admission, reclaim pinning, undo trim, and session-undo parity vs full clone.

**Non-goals (this change):**

- Jam capture (D13), scenes, LFO.
- SD v4 wire format version bump.
- Dynamic growth of `POOL_CHUNK_COUNT` beyond current pool (phase-2 follow-up; admission + reclaim
  first).
- Full PSRAM pool walks on record/overdub stop hot path (per
  [`LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../../../docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md)).

**Depends on:** shipped **m8-edit** / **timeline-pass-model**. **Recommended after**
`loop-ownership-hardening` (slot resolution + SD integrity) and **`note-edit-session-undo-gpio`**
(kind-boundary **`E:`** pushes + stabilized **`EditManager`** commit path) — not hard blockers for
drafting.

**Related:** **`note-edit-session-undo-gpio`** owns **`E:`** / **`U:`** UX and kind-boundary push
policy. This change owns global **`U:`** depth, pass storage admission, and **small `E:`** snapshots
(task group 9) — see design D9–D10.

## Capabilities

### New Capabilities

- `loop-event-pool-admission`: O(1) chunk pool stats, reserve policy, `PoolExhausted` seal outcome.
- `pass-reclaim`: Reference-aware reclaim of **Disabled** passes and **ClearSlot** snapshot memory.
- `undo-memory-trim`: Memory-pressure undo stack trim with preferred depth 99.

### Modified Capabilities

- `timeline-passes`: Capture and **editPass** admission by memory budget; remove fixed per-slot pass
  row caps as requirements.
- `note-edit-session-undo`: **`NoteEditSessionUndoStack`** stores **EditChange** + **focus** entries;
  undo rebuilds via **materialize** + **apply** (not full **cloneShared** stacks).

## Impact

| Area | Files (primary) |
|------|-----------------|
| Chunk pool | `LoopEventStore.h`, `LoopEventStore.cpp`, `LoopPasses.h` |
| Pass lifecycle | `Loop.cpp`, `Loop.h` |
| Undo | `TrackUndo.cpp`, `GlobalUndoStack.h` |
| Orchestration | `TrackManager.cpp`, `Track.cpp`, `EditManager.cpp`, `main.cpp` |
| Config | `Globals.h` |
| Tests | new `test_pool_budget` or extend `test_loop_event_store`, `test_edit_apply` |
| Docs | `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md` (admission + reclaim policy) |
| Supersedes | `loop-ownership-hardening` task group 4 (fixed edit cap) — parked there |
| Related | `note-edit-session-undo-gpio` (**`E:`** UX); task group 9 **EditChange** session snapshots |
