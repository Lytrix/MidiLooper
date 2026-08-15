## Context

DEC-036 Layer D 3b shipped: overdub **entry** copies a clean `visualCache.notes` ([`045556`](../../../captures/session_20260814_045556.log) 2214 µs). D1 eager `rebuildEffectiveEventStore` → full `materializeToEventVector` was withdrawn because `invalidateCaches` discarded the warm store.

Remaining cost: after overdub **commit**, `markDisplayCachesStale` dirties all bars; `CommittedEventRange::inWindow` still walks every active pass list. Short loops still `rebuildVisualCacheFromPasses` (`VCACHE,full`).

DEC-016 already requires representation × interval. The missing owner is query-time resolution of the **active** pass set plus edit history.

Brownfield: [`loop_event_sourced_resolution_architecture.md`](../../../docs/Plans/loop_event_sourced_resolution_architecture.md), [DEC-037](../../../docs/DECISION_LOG.md#dec-037-loop-content-resolution-parallel-prototype).

## Goals / Non-Goals

**Goals:**

- Native `LoopContentResolution` with `resolveState` / `resolveWindow` primary and `resolveNotes` as a derived consumer.
- Canonical stress fixture before proving `P0 + P1`.
- Indexes so candidate **find** does not walk pass lists; checkpoints so `resolveState` does not replay from tick 0.
- Three gates (correctness, complexity, device worst-case latency) before any production consumer swap.
- Keep `materializeToEventVector` as the production path until those gates pass.

**Non-Goals:**

- Deleting materialize; MIDI hot-path resolution; persisted D3 checkpoints; D4 load publication; GUS replacement; overlay picker; interval reservation; replacing `NoteGeometryResolver`.

## Decisions

1. **New owner, not another cache on `Loop`.** `LoopPasses` stays content authority. `LoopContentResolution` owns effective-state queries. Alternative rejected: evolve `passesMaterializedStore_` again (D1).
2. **Name `LoopContentResolution`.** Rejected: `Resolver`, `LoopContentResolver` (`NoteGeometryResolver` collision).
3. **Three event roles:** `RawMidiEvent`, `EditAction`, `ResolvedEvent`. Today’s `MidiEvent` is the MIDI shape; `EditPass` is the edit-action row.
4. **Primary queries are state and window, not notes.** Playback consumes `ResolvedEvent`. `resolveNotes` is a projection.
5. **Active set, not all stored passes.** Committed content is immutable; Active/Disabled is mutable history state.
6. **Two cost legs.** Find via index → candidates. Resolve candidates → layer semantics. `for (pass : passes) if intersects(window)` fails the complexity gate even if the window is small.
7. **`checkpointIntervalTicks` is a measured performance parameter, not a semantic property of the loop.** Native 1-bar, device 8-bar, device 16-bar, and a later adaptive stride MUST produce identical `resolveState` answers for the same active history. Density only bounds replay work and RAM.
8. **A checkpoint is a jump point, not a copy of the resolved loop.** `soundingAt` MAY exist only at a sparse stride. A full sounding-state snapshot at every bar (1847 spans × 68 bars on the `035414` class — [`225351`](../../../captures/session_20260814_225351.log)) is another O(history) derived store and fails the architecture. `spans` + `startsByTick` are currently sufficient as the base index for `resolveState`; the device probe will measure whether additional indexing is required.
9. **Determinism.** Same active history ⇒ same answer, independent of cache, chunks, checkpoint density, or prior order.
10. **Chunks are packing.** `LoopEventStore` chunks are not resolution units.
11. **Layer semantics reuse DEC-031/032.** Do not invent a new overlap model.
12. **Failure gate.** If the prototype cannot show a materially better scaling model without another O(history) derived owner, stop and implement range-dirty cache + tick index on existing owners. A weak first tick index does not by itself disprove the architecture. Copying sounding state at every checkpoint **does** disprove that checkpoint representation.
13. **Device 5.1/5.2 order (after [`225744`](../../../captures/session_20260814_225744.log) short-loop `lcr` and [`225351`](../../../captures/session_20260814_225351.log) heap Critical).** Sparse `soundingAt` first; LCR MUST NOT consult advisory pressure; **then** split `prepareRebuildSpans` (`materializeActive` + `reconstructDisplayNotes` MUST NOT remain one idle slice); then measure a selected loop **>63 bars** and `resolveState` replay. Size probe accepted: [`115750`](../../../captures/session_20260815_115750.log) (139 bars). Arm cap stays off. IndexCommit and RebuildSpans MUST yield at `kDeviceGateSliceBudgetUs` (50 ms). Do not persist. Do not put resolution on overdub or MIDI realtime paths.

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| Prototype becomes a second O(history) store that `invalidateCaches` kills | Complexity gate; never cascade `invalidateCaches` onto the prototype; range caches only |
| Per-bar `soundingAt` copies the resolved loop ([`225351`](../../../captures/session_20260814_225351.log) Critical / reboot) | Sparse stride; abort under pressure; checkpoint must not scale with bars × notes |
| `prepareRebuildSpans` stalls MIDI/OLED even if checkpoint RAM is fixed | Split materialize/reconstruct; IndexCommit and span emplace yield at `kDeviceGateSliceBudgetUs`. Arm cap stays off. |
| `resolveNotes` becomes the center (play Notes) | Spec: `resolveNotes` is a derived consumer; correctness vs materialize events first |
| First tick index is slow | Failure gate distinguishes poor index from architectural failure |
| Layer semantics drift from overdub overlap | Native equivalence vs `materialize` + `reconstruct` on the overlap spec |
| Firmware wired too early | Native-only until all three gates; 3b overdub copy stays |

## Migration Plan

1. Native stages 0–8 against the canonical fixture.
2. Device stage 9 (`035414` class) — worst-case µs.
3. If all three gates pass: dirty overdub fallback → idle visual slices → long-loop playback gather. Short-loop / NOTE_EDIT hydrate last.
4. Rollback: leave production on materialize; delete or isolate the prototype module.

## Open Questions

- Device sparse stride: first probe uses **8 bars** (`kDeviceCheckpointBarStride`). Measure replay µs (5.8) before trying 16.
- Whether `spans` + `startsByTick` remain the long-term `resolveState` index, or a later tick/event index is required — measure on the device probe; do not lock a third structure yet.
- Whether `ResolvedEvent` stays a `MidiEvent` alias or a distinct type — pinned at Stage 1 as an alias; no fourth synonym.
