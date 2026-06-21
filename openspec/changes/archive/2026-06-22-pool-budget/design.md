## Context

Canonical MIDI per slot lives in **`passes[]`** (**recordPass**, **overdubPasses[]**, **editPasses[]**).
Capture MIDI is chunk-backed via **`LoopEventStore`** (global pool, `POOL_CHUNK_COUNT = 512`).
Global undo is per **track** (`GlobalUndoStack`); pass undos disable rows but today never free chunks.

Fixed caps (`MAX_UNDO_HISTORY`, `MAX_CAPTURE_PASSES_PER_LOOP`) were reduced from 99 → 25 for heap
pressure with 8×8 slots ([`docs/plans/reduce_undo_and_lazy_loop_b89758a6.plan.md`](../../../docs/plans/reduce_undo_and_lazy_loop_b89758a6.plan.md)).
That trades away valid edge cases when memory is available.

Brownfield constraints ([`LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../../../docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md)):

- Hot stop path: wrap-window only; no `MemoryMonitor` PSRAM walks on record/overdub stop.
- Undo restore: **`cloneShared()`** on snapshots — unchanged.
- Full validate: deferred — unchanged.

## Goals / Non-Goals

**Goals:**

1. Admit capture seal and **editPass** save based on chunk/heap headroom, not pass row count.
2. Prefer **99** undo entries when memory allows; trim only under pressure.
3. Reclaim **Disabled** pass rows and orphaned **ClearSlot** snapshots to free chunks/heap.
4. O(1) admission checks on hot paths; reclaim orchestration on idle or single retry after failure.

**Non-Goals:**

- Growing `POOL_CHUNK_COUNT` at runtime (follow-up).
- Changing **passes** SD v4 schema.
- JamRecorder / D13.

## Decisions

### D1 — Chunk admission in `LoopEventStore`

**Choice:** Add O(1) helpers on **`LoopEventStore`**:

- `usedChunkCount()`, `freeChunkCount()`
- `canAllocChunkWithReserve()` — true when `freeChunkCount() > PassConfig::CHUNK_RESERVE`

**Remove** `sealCapture` check against `MAX_CAPTURE_PASSES_PER_LOOP`.

**Seal failure:** rename **`AtPassCap`** → **`PoolExhausted`** when alloc cannot succeed.

### D2 — Heap admission for `saveNoteEditPass`

**Choice:** Before appending to **`passes.editPasses[]`**, check:

```text
MemoryMonitor::getFreeHeap() >= Config::HEAP_RESERVE_BYTES + estimatedEditPassBytes(changes)
```

`getFreeHeap()` is O(1) (sbrk). No fixed **editPass** row count.

On failure: log WARNING, return **`kInvalidEditPassId`**.

### D3 — Config: preferred depth and reserves (not caps)

**Choice** in `Globals.h` / `PassConfig`:

| Constant | Default | Role |
|----------|---------|------|
| `PREFERRED_UNDO_DEPTH` | 99 | Target undo entries per track when no pressure |
| `MIN_UNDO_DEPTH` | 8 | Try to keep at least this many when trimming |
| `ABSOLUTE_MAX_UNDO_ENTRIES` | 512 | Overflow safety rail |
| `CHUNK_RESERVE` | 16 | Chunks held back for playback |
| `HEAP_RESERVE_BYTES` | 32 KiB | Headroom for edit vectors + undo metadata |

**Remove** `MAX_UNDO_HISTORY` and `MAX_CAPTURE_PASSES_PER_LOOP` as admission gates (may keep
deprecated aliases briefly for tests — delete in same change).

### D4 — Reclaim on `Loop` (extend `freeActiveCapturePassChunks` pattern)

**Choice:** Add on **`Loop`**:

- `reclaimDisabledCapturePass(PassId id)`
- `reclaimUnreferencedDisabledEditPasses(const ReferencedEditPassIds& refs)`
- `reclaimUnreferencedDisabledCapturePasses(const ReferencedCapturePassIds& refs)`
- `reclaimUnreferencedDisabledPasses(const PassReferenceSet& refs)` — single slot entry

**Policy:** Only **Disabled** rows whose ids are **not** in the reference set. Free chunks via
staging **`LoopEventStore`** adopt → `clear()` (same as **`freeActiveCapturePassChunks`**).

**editPass** rows: erase vector entries (no chunks); shrink **`EditChangeList`** storage.

### D5 — Reference set from `TrackUndo`

**Choice:** Anonymous helper in **`TrackUndo.cpp`**:

`collectReferencedPasses(TrackManager&)` scans all tracks' **`GlobalUndoStack`**:

| `UndoEntryKind` | Pins |
|-----------------|------|
| `RecordPassAdded` / `OverdubPassAdded` | `slotIndex` + `passId` |
| `NoteEditPassClosed` | each id in `noteEditPassIds` |
| `ClearSlot` | all pass ids + chunk refs in `beforeSnapshot` / `afterSnapshot` |
| `LoopBoundaryChange` | geometry only — no pass pins |

Redo branch entries still in `entries[]` pin their passes until trimmed.

### D6 — `TrackManager::reclaimUnreferencedDisabledPasses`

**Choice:** One orchestrator:

1. `refs = collectReferencedPasses(*this)`
2. For each track, each slot: `loop.reclaimUnreferencedDisabledPasses(refs)`

**Call sites:**

| Caller | When |
|--------|------|
| `main.cpp` idle block | `!timingCriticalTrackActive` (with deferred validate / save) |
| `Track::finalizeCommitSideEffects` | After `SealFailed` — reclaim + one `commitCapturePass` retry |
| `EditManager::commitEditAction` | After invalid `saveNoteEditPass` — reclaim + one retry |

**Do not** call from `LoopEventStore::allocChunk` (avoids upward dependency).

### D7 — Memory-aware undo trim

**Choice:** Replace `trimGlobalUndoHistory` with `trimUndoStackForMemory(Track&)`:

Trim oldest entries while **all** true:

- `overUndoMemoryPressure()` — chunk reserve violated OR heap below reserve OR
  `entries.size() > PREFERRED_UNDO_DEPTH` with pressure, AND
- `entries.size() > MIN_UNDO_DEPTH`

After each dropped entry: `TrackManager::reclaimUnreferencedDisabledPasses()`.

Also run reclaim after `dropRedoBranch`, `eraseUndoEntriesForSlot`.

**Pressure signals (O(1) only on hot path):** chunk counts + `getFreeHeap()`. Full PSRAM stats
only in existing idle `MemoryMonitor::logStatus()` (60s).

### D8 — Dynamic pool growth (deferred)

**Choice:** Park runtime `POOL_CHUNK_COUNT` expansion to a follow-up task group. This change
ships admission + reclaim on the fixed pool first.

### D9 — Committed edits vs live session store (clarification)

Two storage shapes must not be conflated:

| Phase | What is stored | Memory shape |
|-------|----------------|--------------|
| **Committed** (`saveNoteEditPass`) | **EditChange** list on **`editPasses[]`** | Small heap metadata per **editPass** row |
| **Live session** (`NoteEditSession.store`) | Full materialized loop MIDI for preview/edit | **`LoopPasses::materialize`** → **`loadFromFlat`** into **`LoopEventStore`** chunks |

On **`openNoteEditSession`**, firmware calls **`loop.rematerializeEditView(noteEditSession.store)`**,
which materializes **all active capture passes + active editPass overlays** into the session store.

**Today:** **`NoteEditSessionUndoStack`** pushes call **`store.cloneShared()`** on that **full session
store** — each **`E:`** step allocates new pool chunks proportional to **materialized event count**
(up to **`kMaxDepth = 32`**), which is problematic for long loops (e.g. 128 bars).

**`note-edit-session-undo-gpio`** reduces push frequency (geometry-kind boundaries). **D10** replaces
per-entry full clones.

### D10 — Small session undo entries (EditChange + focus)

**Choice:** Replace **`NoteEditSessionUndoStack`** full **`LoopEventStore`** snapshots with
**`SessionUndoEntry`** containing:

| Field | Source | Size driver |
|-------|--------|-------------|
| **`EditChangeList changes`** | **`buildPreCommitEditChanges`** (or equivalent at kind boundary) | Edits + overlap notes in scope |
| **`NoteEditFocus focus`** | Deep copy of session focus at push | Overlap map size, not loop length |
| **`NoteEditSelection`** (or bracket fields) | **`NoteEditSessionState`** / **`selectedNoteIdx`** | O(1) |

**Push (kind boundary, after `note-edit-session-undo-gpio`):**

1. Resolve overlap in session store if needed (same as pre-commit path).
2. Build **`changes`** = net diff from materialized **`passes`** baseline to current session state.
3. Append **`SessionUndoEntry{changes, focus, selection}`** — **no `cloneShared`**.

**`sessionUndo` / `sessionRedo`:**

1. **`loop.rematerializeEditView(noteEditSession.store.mutStore())`**
2. **`applyEditChangeList`** for the target entry's **`changes`** (cumulative or per-entry per stack
   design — prefer **per-entry net diff** stored at push; apply entries **`[0 .. cursor)`** on restore)
3. Restore **`focus`** + **`applyUndoRedoLanding`** / **`syncNoteEditSessionStateToUi`**

**Undo stack memory:** depth × (**EditChange** + **focus**), not depth × loop event count. **One**
live session store remains materialized during edit; rebuild cost is paid on undo/redo button only.

**Admission (task 9):** heap check on **`EditChangeList`** + **`NoteEditFocus`** size before push;
pressure-based trim of oldest entries; **`PREFERRED_SESSION_UNDO_DEPTH`** when affordable (parallel
to global **`PREFERRED_UNDO_DEPTH`**).

**Validation gate:** native parity tests — **`rematerialize` + apply** must match **`cloneShared`**
restore for overlap scenarios (hidden/shorten/restore, move → pitch → move back) before removing
**`cloneShared`** push path.

**Depends on:** **`note-edit-session-undo-gpio`** kind-boundary **`pushSessionUndoOnKindChange`**
(task group 3 there defines when push runs).

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| Reclaim frees pass still needed for redo | Reference set includes all undo entries at/after cursor |
| Reclaim during PLAYING causes hitch | Idle reclaim primary; commit-path retry once only |
| 99 undo depth + many ClearSlot entries exhaust heap | ClearSlot remains heavy; trim drops oldest + reclaim snapshots |
| Seal retry after reclaim on hot path | Single retry; reclaim is bounded per slot |
| Heavy **`E:`** session on large loop | **D10** EditChange + focus entries; kind-boundary pushes from **gpio** change |
| Session undo parity regression | Native clone vs rematerialize+apply matrix before removing **cloneShared** |

## Migration Plan

1. Ship firmware replacing fixed caps with admission + reclaim.
2. No SD format bump.
3. **`loop-ownership-hardening`** task group 4 must not ship before this (parked there).

## Open Questions

1. **TBD:** User-visible feedback when `PoolExhausted` or edit admission fails (serial only vs display).
2. **TBD:** `CHUNK_RESERVE` / `HEAP_RESERVE_BYTES` defaults after first HITL long-session test.
3. **TBD:** Phase-2 dynamic `POOL_CHUNK_COUNT` growth trigger threshold.
4. **TBD:** **`PREFERRED_SESSION_UNDO_DEPTH`** default (align with **`PREFERRED_UNDO_DEPTH`** or lower).
