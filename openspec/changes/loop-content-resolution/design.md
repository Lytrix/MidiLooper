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
8. **A checkpoint is a jump point, not a copy of the resolved loop.** `soundingAt` MAY exist only at a sparse stride. A full sounding-state snapshot at every bar (1847 spans × 68 bars on the `035414` class — [`225351`](../../../captures/session_20260814_225351.log)) is another O(history) derived store and fails the architecture. `spans` plus a **tick-ordered span-boundary index** (start **and** exclusive-end; `spanBoundaries`) are sufficient for `resolveState` tail replay. 5.15c: C-order append + `stable_sort` by tick.
9. **`TickIndex` owns event-range lookup.** Contract: `tick ∈ [begin, end)` → Active `(passId, eventIndex)`. Flat A (`tickEvents`, C-order append + `stable_sort` by tick). 5.17d device [`161355`](../../../captures/session_20260815_161355.log). 5.17e removed `byTick`. [`170024`](../../../captures/session_20260815_170024.log) `iapp=201044` is reserved bulk append, not a remaining map. Do not collapse this index into `spanBoundaries`.
10. **Derived-index storage (DEC-037 invariant, not a new DEC).** Derived indexes on the target device use contiguous/bulk PSRAM storage. Per-entry dynamic allocation into PSRAM associative containers is prohibited on realtime-adjacent index construction paths. Pick the representation from the query contract. Do not add B unless a measured flat query is too expensive. Measured: `spanBoundaries` (5.15), `tickEvents` (5.17), `channelByNoteId` (5.7c **FROZEN** [`170024`](../../../captures/session_20260815_170024.log)). `pair` / `byNoteId` / `openOnByPitch` / `recon` are 5.18 — flatten from the query, not the container type.
11. **Determinism.** Same active history ⇒ same answer, independent of cache, chunks, checkpoint density, or prior order.
12. **Chunks are packing.** `LoopEventStore` chunks are not resolution units.
13. **Layer semantics reuse DEC-031/032.** Do not invent a new overlap model.
14. **Failure gate.** If the prototype cannot show a materially better scaling model without another O(history) derived owner, stop and implement range-dirty cache + tick index on existing owners. A weak first tick index does not by itself disprove the architecture. Copying sounding state at every checkpoint **does** disprove that checkpoint representation.
15. **Device 5.1/5.2 complete; Stage 6 consume-only + 6A/6B/6C.** 5.15–5.18 **FROZEN**. **5.1 PASS** [`173842`](../../../captures/session_20260815_173842.log). **5.2 PASS** [`180624`](../../../captures/session_20260815_180624.log) `begin_capture` 10050 µs. Overdub start/stop MUST NOT synchronously construct, sort, checkpoint, or resolve LCR state — consume already-prepared derived state only. LCR is the producer of prepared derived state, not a replacement for `overdubSourceView`. Keep 3b copy. `< 3 ms` is a regression target vs 3b 2214 µs; `< 50 ms` hard gate. Experiment: **6A** idle display range, **6B** commit invalidation, **6C** overdub source. Do not persist. Do not call resolution from `handleMidiInput`. No B. Firmware waits for an explicit implement request.

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| Prototype becomes a second O(history) store that `invalidateCaches` kills | Complexity gate; never cascade `invalidateCaches` onto the prototype; range caches only |
| Per-bar `soundingAt` copies the resolved loop ([`225351`](../../../captures/session_20260814_225351.log) Critical / reboot) | Sparse stride; abort under pressure; checkpoint must not scale with bars × notes |
| `prepareRebuildSpans` stalls MIDI/OLED even if checkpoint RAM is fixed | Split materialize/reconstruct; IndexCommit, pairing, reconstruct, project, span append, and `RebuildPrepare` materialize batch `kDeviceGateEventsPerSlice`. Arm cap stays off. |
| `resolveNotes` becomes the center (play Notes) | Spec: `resolveNotes` is a derived consumer; correctness vs materialize events first |
| Per-entry PSRAM map/multimap insert on derived indexes | DEC-037 derived-index storage invariant; 5.15 / 5.17 / 5.7c **FROZEN**. `pair` / `byNoteId` / `openOnByPitch` are 5.18 |
| Layer semantics drift from overdub overlap | Native equivalence vs `materialize` + `reconstruct` on the overlap spec |
| Firmware wired too early | Native-only until all three gates; 3b overdub copy stays; overdub start/stop never construct/sort/checkpoint/resolve LCR |
| Hidden `ensure*` LCR rebuild on overdub start/stop | Consume-only invariant: any synchronous construct/sort/checkpoint/resolve is a violation |

## Migration Plan

1. Native stages 0–8 against the canonical fixture.
2. Device stage 9 (`035414` class) — worst-case µs.
3. If all three gates pass: **6A** idle display range via `resolveWindow` (old path oracle) → **6B** commit marks affected ranges only → **6C** overdub source from prepared LCR with 3b copy fallback. Long-loop playback / NOTE_EDIT last. Never construct/sort/checkpoint/resolve LCR on overdub start/stop. **6A PASS** [`185931`](../../../captures/session_20260815_185931.log). **6B PASS** [`192334`](../../../captures/session_20260815_192334.log). **6C native** (device score next).
4. Rollback: leave production on materialize; delete or isolate the prototype module.

## Open Questions

- Device sparse stride: first probe uses **8 bars** (`kDeviceCheckpointBarStride`). Measure replay µs (5.8) before trying 16.
- Pair leftover **closed** [`173842`](../../../captures/session_20260815_173842.log). **5.1 PASS** same capture. **5.2 PASS** [`180624`](../../../captures/session_20260815_180624.log) `begin_capture` 10050 µs. **6A PASS** [`185931`](../../../captures/session_20260815_185931.log) `match=1`. **6B PASS** [`192334`](../../../captures/session_20260815_192334.log). **6C native** (device score next vs 3b 2214 µs). After 6C device: MIDI Input Gap > 50 ms in [`192334`](../../../captures/session_20260815_192334.log) (`midi_gap` 135 / 119 / 138 ms, `clockrate` 47).
- Whether `ResolvedEvent` stays a `MidiEvent` alias or a distinct type — pinned at Stage 1 as an alias; no fourth synonym.
