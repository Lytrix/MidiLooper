# Loop layer history persistence — architecture

**Status:** Shipped — Stage 3 + follow-up device PASS; OpenSpec archived 2026-08-14  
**Date:** 2026-08-14  
**Decision:** [DEC-035](../DECISION_LOG.md#dec-035-loop-persists-content-only)  
**OpenSpec:** archived `openspec/changes/archive/2026-08-14-loop-content-history-persistence/` (Layer A only)  
**Cursor plan:** `~/.cursor/plans/scoped_undo_persist_aa3b7a01.plan.md`  
**Bug (functional failure):** [#32](https://github.com/Lytrix/MidiLooper/issues/32) — [`overdub_stop_playing_midi_dump_bugfix.md`](overdub_stop_playing_midi_dump_bugfix.md)  
**Task (migration):** [#33](https://github.com/Lytrix/MidiLooper/issues/33)  
**Does not authorize:** Stage 3b GUS replacement, Layer B–D firmware, interval reservation, early USB / tier-0 restore

---

## North star

> Persistence knows content. Replay reconstructs current Loop state. Editing derives undo/redo from ordered content plus grouping rules. Playback knows ranges.

The Loop persists content only. Undo/redo is runtime/editor behavior derived from that content. It is explicitly outside the persistence model.

Persisted content and runtime-resident content are different. The Loop may own all content on disk without keeping every layer in the chunk pool.

## Rejected concepts

- Persisted `LoopUndoHistory` / `undo_TT_SS.bin`
- `PassStateChange`, persisted history position, persisted undo/redo records, persisted editor history
- `Source` as a domain noun in docs, comments, or types (`ClockSource` already exists)
- `WINDOW_READY` / `windowHydrated` as domain concepts
- Full-loop `COMMITTED` as the playback gate
- `Available` / `Buffered` as stored lifecycle flags
- Persisted `stateRaw` / pass Active-Disabled fields (WIP; no old-firmware reader requirement) — removal is a Stage 1 proof, not an assumption

## Three models

**Persistent model** — durable source of truth:

```text
Loop
 └── ordered immutable content-layer records
      ├── RecordPass
      ├── OverdubPass
      ├── EditPass          (committed noteEditPass batch)
      └── LoopGeometry      (LoopGeometry.id)
```

Undo/redo is not persisted. After reboot, reconstruct the current Loop at the **tip**. Mid-session editor cursor does not survive. Redo is empty at load. Undo can walk content until empty.

Redo follows today's `TrackUndo` (`dropRedoBranch` + `applyRedoEntry` in [`TrackUndo.cpp`](../../src/TrackUndo.cpp)): undo does not destroy content; new work after undo drops the redo tail and reclaims.

Display `U:nn` is recomputed on load from derived undo steps, not restored from a saved count.

**Reconstructed model** — two derivations from the same content:

```text
content ──► replay ──────────────► current Loop state
     └──► editing derivation ───► undo/redo semantics
```

Undo is derived from ordered content records plus grouping rules (companions, edit batches, geometry). Rebuilding notes without operation boundaries is a failed Stage 1/2.

**Editing model** — runtime only. `GlobalUndoStack` remains the in-session owner until a later Stage 3b DEC. Load must still reconstruct editing semantics (Stage 2).

**Streaming model** — runtime loading only (Layer D; not Layer A firmware):

```text
Loop content → TickRange request → BufferedData → isRangeAvailable(demand) → PlaybackWindow
```

`LoadLoopJob` is the mechanism, not the domain.

```text
PlaybackWindow ⊆ AvailableData ⊆ BufferedData ⊆ Loop content
```

Loop content is the logical/content domain, not an in-RAM collection. `BufferedData` is the materialized runtime subset (a set of ranges, not necessarily one interval).

**Named invariant:** `isRangeAvailable(W)` is true only when the engine can deterministically execute W without consulting unavailable Loop content (wrap, look-ahead, open notes).

`COMMITTED` = full current-state reconstructed. It is not the play gate.

## Publication rule (recorded; Layer D only)

A loop may become playback-visible before full reconstruction **only when the complete playback demand range is available**. DEC-026 slot-level lazy stays. Do not restore 2026-07-18 early USB / `audibleBootSetReady_`. OpenSpec `lazy-slot-hydration` conflict is a Layer D reassessment, not this change.

## Layer A evidence

The first persistence-level lever is deleting `DeferredSaveStage::UndoStacks`. That establishes the new floor (`GlobalMeta` 4 slices / 23.7 ms). `LoopPersist` can still dominate stop latency.

Evidence: [`112104`](../../captures/session_20260813_112104.log) / [`163422`](../../captures/session_20260813_163422.log) / [`154823`](../../captures/session_20260813_154823.log).

`applyUndoEntry` (except `ClearSlot` / `LoopBoundaryChange`) is `setCapturePassState` / `setEditPassState`. The bundle undo payload persists bookkeeping about state the loop file already stores.

Chunk pool is the RAM bound: ~342 chunks for a 64-bar ~46-pass loop vs `POOL_CHUNK_COUNT = 512` shared across 8 tracks.

Boot `load_frame` is a separate owner. Layer A does not target it.

## Layers

| Layer | Stages | Gate |
|-------|--------|------|
| A | 1–3 | Content history ≠ persisted undo. Clear-as-unlink is **not** in this gate. |
| B | 4–5 | Set owns slot → `LoopId`; Loop owns content; append-structured journal |
| C | 6 | Replay becomes bounded (checkpoint + tail) |
| D | 7 | Load becomes range-driven. Second design. |

## Order

```text
Stage 0   architecture + gates          (this document)
Stage 1   prove content contains enough
Stage 1b  LoopGeometry content record
Stage 2   load-time replay + history derivation
Stage 3   delete persisted UndoStacks
Stage 3b  separate DEC: replace GlobalUndoStack
Stage 4   Set owns slot identity / clear-as-unlink
Stage 5   append-structured journal
Stage 6   checkpoint + tail
Stage 7   range-first playback
```

### Stage 1 — Content sufficiency (shipped 2026-08-14)

Native fixtures: `test/test_loop_content_history/`. Derivation: `deriveContentUndoUnits` in [`LoopContentHistory.h`](../../include/LoopContentHistory.h).

| `UndoEntryKind` | Persisted content | Boundary from content? |
|-----------------|-------------------|------------------------|
| `RecordPassAdded` | `recordPass.id` | Yes |
| `OverdubPassAdded` | overdub ids sharing `overdubSessionIndex` (!= 0) + following `editPassIndex == 255` companions | Yes — index `0` is one unit per pass (legacy) |
| `NoteEditPassClosed` | consecutive rows with the same `editPassIndex` | Yes — persist unit is the committed noteEditPass batch (`primaryPassId` = first row id) |
| `ControlChangeEditPassClosed` | same, `EditPassType::ControlChange` | Yes — same batch rule |
| `LoopBoundaryChange` | `LoopGeometry.id` + ticks on `passes.loopGeometries` | Yes — Stage 1b |
| `ClearSlot` | not Loop content | Layer B (`lastUnlinkedSlotLink`) |

**Prefix invariant:** holding. Omitting a Disabled suffix materializes the same notes as leaving those rows Disabled (`test_active_prefix_materialize_matches_omitted_suffix`). Persisted Active/Disabled is unnecessary if the file stores only the effective prefix.

**Retracted — loop-lifetime undo-unit id.** Session `editPassId` / `editPassIds` are temporary runtime for in-session `E:` undo. That path does not change. On commit, the batch is one noteEditPass (`closeNoteEditPass`). Reboot during an active NOTE_EDIT already flattens via `processEditAutosave` → `markCurrentEditBatchDurable` → `pushNoteEditPassClosed` (HITL PASS [`session_20260805_212234`](../../captures/session_20260805_212234.log)). Do not add a second flatten path or a new undo-unit id field.

**Stage 1b — `LoopGeometry` content record (shipped 2026-08-14).** `LoopGeometry` sits next to `recordPass` / `editPasses` with `LoopGeometry.id` from `nextPassId_++`. `commitPendingLoopGeometry` appends a record and still pushes in-session `UndoEntry` `LoopBoundaryChange` until Stage 3b. `UndoLoopGeometry` remains the GUS payload shape. Persist is an additive `GEO1` tail after `EPT3`; cards without the tail still load.

Stage 2 can derive load-time editing state from content including `LoopGeometry`. Keep writing today's bundle undo stack. `GlobalUndoStack` stays in-session authority.

### Stage 2 — Load-time reconstructed editing state (shipped 2026-08-14)

After Loop load / GUS file read, `TrackUndo::rebuildSlotFromLoopContent` replaces that slot's pass-undo entries with `buildContentUndoEntries` from Active content. Cursor is the tip; redo is empty. `ClearSlot` entries are preserved (Layer B). Display `U:nn` is `undoDepthForLoop` over that filled GUS.

`LoopGeometry` now stores before ticks so a loaded `LoopBoundaryChange` can walk back. Undo/redo of that kind disables/enables the matching `LoopGeometry.id`. Keep writing today's bundle undo stack.

### Stage 3 — Delete persisted UndoStacks (device PASS [`030147`](../../captures/session_20260814_030147.log) 2026-08-14)

Gated on Stage 2 + exit-bake PASS [`025322`](../../captures/session_20260814_025322.log). Footer writes empty GUS headers; `admitLoopUndoHistory` is a no-op; leftover `UndoStacks` stage does not walk live entries. Legacy bundle-undo **read** remains. In-session undo still uses `GlobalUndoStack`.

Device: zero `LoopUndoHistory` / `UndoStacks` ([`030147`](../../captures/session_20260814_030147.log)). Follow-up [`032227`](../../captures/session_20260814_032227.log): overdub stop admits `LoopPersist` only (~139 ms); no `PERS,bundle,SlotMeta`. Reboot undo on slot 5 works (`entries=17` then `16`).

### Stage 3b — later DEC

Replace `GlobalUndoStack` with derived Loop editing state. Not a silent DEC-024 Phase 2.

### Stages 4–7 — later layers

Clear-as-unlink is Set last-state (`lastUnlinkedSlotLink`), not Loop undo history. Journal is append-structured (append on new work; drop/reclaim redo tail after undo). Do not promise append-only.

## Clear product rule (Layer B; recorded now)

Only the most recent clear is undoable as a Set relink. After relink, pass undo/redo walks Loop content to empty. Display after unlink: `U:--`.

## Invariants held throughout

- `StorageManager` stays the only persistence owner (DEC-008).
- Admission stays `admit*` + `PersistenceWorkQueue`.
- `handleMidiInput()` call sites and ordering unchanged.
- No interval reservation, no MIDI catch-up suppression, no early USB / tier-0 restore.
- Layer D firmware is a second design. Do not mix into Layer A.

## Architecture gate (Layer A)

- Owner: `StorageManager` persist; `Loop` content; `TrackUndo` runtime until 3b
- Invariant: Loop persists content only; undo/redo is derived
- Ownership change: persist payload deleted in Stage 3; 3b replaces GUS (later DEC)
- Transition change: NO for Layer A
- Reuse: YES — extend `LoopPasses` / `LoadLoopJob` / `TrackUndo`; no new Manager
- Phase scope: Stages 1–3 only

## Pre-implementation review (Stage 1b)

### Ready
- Reboot-during-edit flatten already exists (`markCurrentEditBatchDurable`). No new NOTE_EDIT path.
- `LoopGeometry` is a `LoopPasses` content record; persist owner stays `StorageManager`.

### Resolved
| Topic | Decision |
|-------|----------|
| Session `editPassIds` | Unchanged (E: only) |
| Persist unit | Committed noteEditPass batch |
| `UndoLoopGeometry` | GUS payload until Stage 3b |
| `LoopGeometry` | Content record, not `LoopPass` |

### Open before coding
None.

### Proceed?
YES
