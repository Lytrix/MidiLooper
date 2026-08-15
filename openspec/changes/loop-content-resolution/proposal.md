## Why

Layer D 3b made overdub **entry** cheap when the visual cache is clean ([`045556`](../../../captures/session_20260814_045556.log) `begin_capture` 2214 µs). After a new overdub **commit**, `markDisplayCachesStale` still dirties every bar and windowed gather still walks every active pass list. D1’s eager full flatten plus `invalidateCaches` failed. Do not optimize `materializeToEventVector` again. Prove whether indexed, checkpointed `LoopContentResolution` can make materialization unnecessary on the normal path.

## What Changes

- **New owner `LoopContentResolution`:** query-time effective musical state from the **active** pass set plus edit history. Primary APIs: `resolveState()`, `resolveWindow()`. `resolveNotes()` is a derived consumer, not the architecture center.
- **Parallel native prototype** beside today’s `LoopPasses::materializeToEventVector`. Production MIDI, display, overdub entry, and NOTE_EDIT stay on current owners until three gates pass.
- **Canonical stress fixture first** (64/128 bars, 45+ passes, overlaps, shorten/extend/delete/move, wrap) with history/window/candidate/ops/µs counters. Do not prove the architecture on `P0 + P1`.
- **Indexes + in-RAM checkpoints** at `checkpointIntervalTicks`. `resolveState(tick)` must not replay from tick 0.
- **Vocabulary:** `RawMidiEvent`, `EditAction`, `ResolvedEvent`. Physical PSRAM chunks are packing, not resolution boundaries.

**Non-goals (this change):** deleting `materializeToEventVector`; wiring resolution onto `handleMidiInput`; persisted checkpoint / D3 `StorageManager` write; D4 `LoadLoopJob` publication; replacing `GlobalUndoStack`; overlay picker; interval reservation; RC-J; replacing `NoteGeometryResolver`.

## Capabilities

### New Capabilities

- `loop-content-resolution`: Deterministic `resolveState` / `resolveWindow` / `resolveNotes` from active pass set + edit history; candidate find via index (not pass-list walk); bounded `resolveState` replay via checkpoints; three-gate promotion before production consumers.

### Modified Capabilities

- `long-record-memory-headroom`: the **normal** overdub/edit path MUST NOT rematerialize all historical passes because a new pass was added. Cost after indexing/checkpointing tracks candidate events and affected state, not pass count.

## Impact

- Native-first: new `LoopContentResolution` module + `test/test_loop_content_resolution/`. No MIDI hot-path call sites until gates.
- Production later (gated): **6A** idle display range, **6B** affected-range commit invalidation, **6C** overdub source from prepared LCR. 3b visual-cache copy stays. Overdub start/stop never construct/sort/checkpoint/resolve LCR.
- Brownfield: [`loop_event_sourced_resolution_architecture.md`](../../../docs/Plans/loop_event_sourced_resolution_architecture.md), [DEC-037](../../../docs/DECISION_LOG.md#dec-037-loop-content-resolution-parallel-prototype), DEC-016 / DEC-035 / DEC-036, `openspec/specs/overdub-pass-overlap-resolution/`.
- Formal trigger: **new owner** — see `ARCHITECTURE-REVIEW.md`.
