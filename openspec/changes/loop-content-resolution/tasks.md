## 1. Docs and gates (this session)

- [x] 1.1 Architecture plan — [`loop_event_sourced_resolution_architecture.md`](../../../docs/Plans/loop_event_sourced_resolution_architecture.md)
- [x] 1.2 DEC-037 in DECISION_LOG; NAMING.md vocabulary
- [x] 1.3 Close `loop-effective-event-source` tasks 4.1 / 4.2; update CURRENT_WORK, PROJECT_STATE, DELIVERABLE_TRACKING
- [x] 1.4 This OpenSpec change (proposal, design, specs, ARCHITECTURE-REVIEW, tasks)

## 2. Stage 0 — Canonical fixture

- [x] 2.1 Native suite `test/test_loop_content_resolution/` with one growing fixture: 64 or 128 bars, 45+ passes, multiple channels, same-pitch overlaps, shorten/extend/delete/move, wrap
- [x] 2.2 Counters on every run: events in history, passes in history, events in query window, candidate events, resolution operations, elapsed µs
- [x] 2.3 Baseline comparison path: same fixture through `LoopPasses::materializeToEventVector` + `reconstructDisplayNotes` (correctness oracle)

## 3. Stages 1–5 — Native resolveWindow / resolveState

- [x] 3.1 Module `LoopContentResolution` with `resolveState` / `resolveWindow` primary; `resolveNotes` derived; pin `RawMidiEvent` / `EditAction` / `ResolvedEvent` (alias/`MidiEvent` shape, no fourth synonym)
- [x] 3.2 Stage 1: one pass NOTE_ON/OFF → window + state; notes as projection
- [x] 3.3 Stage 2: two overlapping same-pitch passes (DEC-031/032 layer semantics)
- [x] 3.4 Stage 3: DELETE via EditAction
- [x] 3.5 Stage 4: SHORTEN / EXTEND
- [x] 3.6 Stage 5: MOVE (tick / pitch)
- [x] 3.7 Disabled pass excluded from active set; content unchanged
- [x] 3.8 Determinism: cold vs warm cache identical `ResolvedEvent` sequences
- [x] 3.9 `pio test -e native` including this suite

## 4. Stages 6–8 — Index and checkpoints

- [x] 4.1 Tick (and NoteId as needed) index; candidate find MUST NOT walk every pass list
- [x] 4.2 Prove `commit P(N)` does not traverse P0…P(N-1) except indexed affected regions (complexity gate on the canonical fixture)
- [x] 4.3 In-RAM checkpoints at `checkpointIntervalTicks`; measure the interval on the fixture (do not persist)
- [x] 4.4 `resolveState` at a high tick starts from a checkpoint, never from tick 0
- [x] 4.5 Note spanning two chunks still resolves (chunks are not boundaries)
- [x] 4.6 Loop switch: warm destination `resolveState` at the live playhead; replay ≤ `checkpointIntervalTicks`; no checkpoint rebuild; no tick-0 scan

## 5. Stage 9 — Device gate (after native 2–4)

- [x] 5.3 Record worst-case µs, not only totals (native 64-bar fixture: materialize 145 µs, window 28 µs, rebuild 214 µs, `resolveState` 1 µs). Device `035414`-class numbers are **not** recorded yet.
- [ ] 5.1 `035414`-class worst-case latency: no multi-second MIDI stall, no multi-second OLED stall
- [ ] 5.2 Overdub entry remains cheap (3b copy); no `VCACHE,full` on the normal path; no full materialize after commit

Device probe: sliced `DeviceGateSession` in idle maintenance (`linker/imxrt1062_t41_lcr.ld`). Does **not** consult `MemoryMonitor`. Tick-index / `startsByTick` use `ExternalMemoryFirstAllocator`. Arm waits for any pending slot restore. CrashReport [`115242`](../../../captures/session_20260815_115242.log) `0x6003616C` `_M_emplace_equal` / `0x10`. 30-bar `hist=832` [`112843`](../../../captures/session_20260815_112843.log). >63-bar size [`115750`](../../../captures/session_20260815_115750.log) `hist=2394` `reb=101s`. [`134954`](../../../captures/session_20260815_134954.log) complete `hist=2394`. IndexCommit / pairing / reconstruct span build / display project / RebuildSpans batch `kDeviceGateEventsPerSlice`. Arm cap stays off. Do not persist or put resolution on overdub/MIDI.

- [x] 5.4 Sparse `soundingAt`: keep `spans` + `startsByTick`; device stride **8 bars** (`kDeviceCheckpointBarStride`). Native Stage 7/8 stay at 1 bar. Same `resolveState` answers (`test_stage7_sparse_checkpoints_agree_with_dense`).
- [x] 5.5 Withdrawn: LCR MUST NOT consult advisory pressure. Tick-index / `startsByTick` use `ExternalMemoryFirstAllocator` (CrashReport [`115242`](../../../captures/session_20260815_115242.log) `0x6003616C` `_M_emplace_equal` at `0x10`).
- [x] 5.6 Split `prepareRebuildSpans` into `prepareRebuildResolvedEvents` then `finishRebuildSpansFromEvents` (one idle slice each). Device `RebuildPrepare` / `RebuildSpans`. Native `rebuild()` still composes both.
- [ ] 5.7 Measure selected-loop idle gate on a loop **>63 bars**: `DIAG,lcr` or explicit skip; no `idle_maint` ~50 ms; no 1 s `DFRAME` gaps. Size **PASS**. [`153920`](../../../captures/session_20260815_153920.log) complete `hist=2394` `walk=0` `reb=13803604` `st=13046`. 5.16c `prep` **PASS**. 5.17a–c native pick **A** for `byTick`. Device leftovers: `idx` p1 **121 ms**, `recon` 116 ms startup, `DFRAME` gap **4.38 s**. Do not treat `reb=13.80 s` as the fail.
- [x] 5.8 [`115750`](../../../captures/session_20260815_115750.log) `st=16249` `rep=350` `hist=2394` `walk=0`. Query path does not need a third index. Stall is cold IndexCommit / rebuild.
- [x] 5.9 Arm cap stays off (2026-08-15). 5.7 probe is >63 bars; that is not a reason to restore the 16-bar cap.
- [x] 5.10 Bound device-gate IndexCommit and RebuildSpans to `kDeviceGateSliceBudgetUs` (50 ms, same as 5.7 `idle_maint`). One event / one span per inner step; yield when the budget is exhausted. Reconstruct stays its own slice. Native `test_stage9_sliced_*` PASS. Cap stays off. Superseded as the slice unit by 5.11 — a single PSRAM emplace already exceeds 50 ms ([`121702`](../../../captures/session_20260815_121702.log)).
- [x] 5.11 IndexCommit / RebuildSpans process `kDeviceGateEventsPerSlice` (8) per idle slice. Rate-limited `DIAG,lcr,phase,<name>,pass,<u>,ev,<u>,span,<u>,notes,<u>` on step change and at most 1 Hz. Native `test_stage9_range_*` / `test_stage9_phase_line_*` PASS. Cap stays off.
- [x] 5.12 Slice `pairCapturePassNotes` with `pairCapturePassEventRange` and `kDeviceGateEventsPerSlice`. Phase token `pair`. Native `test_stage9_range_pair_matches_full_pair` PASS. Cap stays off. Reconstruct stays its own slice.
- [x] 5.13 Slice reconstruct span build with `appendCanonicalSpansFromMidi` / `kDeviceGateEventsPerSlice`. `reconstructDisplayNotes` still composes the same loop. Native `test_stage9_range_recon_matches_full` PASS. Project+dedup stays one slice. Cap stays off.
- [x] 5.14 Slice display project with `appendProjectedDisplayNotes` / `kDeviceGateEventsPerSlice`. Phase tokens `proj` / `dedup`. `projectDisplayNotes` still composes the same loop. Native `test_stage9_range_proj_matches_full` PASS. Dedup stays one slice. Cap stays off.
- [x] 5.15a Design: `startsByTick` contract is start **and** exclusive-end; `rebuildNotes` is not `startTick`-ordered; `TickIndex::byTick` is a second PSRAM multimap. Plan: [`loop_content_resolution_span_boundary_index_refinement.md`](../../../docs/Plans/loop_content_resolution_span_boundary_index_refinement.md). Do not change batch size. Do not start 6.x.
- [x] 5.15b Native microbench: flat A (`append` + `stable_sort` by tick) vs C map. Same `resolveState` / oracle, `walk=0`, equal-tick order proven. Host: C emplace 29 µs, A append 1 + sort 4 = 5 µs, A resolve 3 vs C 5. Pick **A**. No B. No A2. No `byTick`. Device swap is 5.15c.
- [x] 5.15c Swap device-gate `startsByTick` to flat `spanBoundaries` (C-order append, one `sort` slice, `stable_sort` by tick). Complete line `app=` / `sort=`. `resolveState` reads the flat list. Device [`151450`](../../../captures/session_20260815_151450.log) `app=5490706` `sort=10202` `reb=13851136` `st=13045` `walk=0`. **5.15 complete.** No A2. No B. Do not touch `TickIndex::byTick` in this change.
- [x] 5.16a Paper phase-budget audit from [`151450`](../../../captures/session_20260815_151450.log): wall vs largest slice vs `DFRAME` gap. Plan: [`loop_content_resolution_device_phase_budget_refinement.md`](../../../docs/Plans/loop_content_resolution_device_phase_budget_refinement.md). No 1.4 s slice found. `prep` is the one-slice 182 ms leftover. `DFRAME` gaps >1 s appear in `idx` / `pair` / `proj` / `spans` while slices stay ≤83 ms (pass 0) / ≤121 ms (pass 1 `idx`). Allocations unmeasured.
- [x] 5.16b Allocation telemetry not added. Not required to pick `prep` as the first slice.
- [x] 5.16c Slice `RebuildPrepare`: `beginRebuildResolvedEvents`, then `materializeActive` at `kDeviceGateEventsPerSlice` (record append + yielded `std::merge`). `applyNoteEditPassSequence` stays one slice after materialize. Native `test_stage9_range_prep_matches_full_prepare` PASS. Device [`153920`](../../../captures/session_20260815_153920.log) `prep` **PASS** — no `loop_rem`; wall 38.65 s cooperative; `reb=13803604` `walk=0`. Do not touch `byTick`, `resolveState`, `EditApply`, 5.1/5.2/6.x.
- [x] 5.17a Contract: `findRawWindow` / `visitTickRange` is `tick ∈ [begin, end)` → Active `(passId, eventIndex)`. Wrap is two ranges. Equal-tick order is `multimap` insertion order. Plan: [`loop_content_resolution_tick_index_flat_event_index_refinement.md`](../../../docs/Plans/loop_content_resolution_tick_index_flat_event_index_refinement.md). Entry is `TickEventEntry { tick, passId, eventIndex }`. Do not import span-boundary concepts. Do not touch `recon` / `pair`.
- [x] 5.17b Native C vs flat A: C-order append + `stable_sort` by tick only. Host (94 entries): C emplace 23 µs, A append 11 + sort 15 = 26 µs, A query 144 vs C 151. Pick **A**. No B. No A2.
- [x] 5.17c Native equivalence: same `byTick` walk, wrap, disabled-pass skip, `resolveWindow` = C = materialize oracle, `walk=0`. Equal-tick OFF-then-ON at 192 preserved. Production `byTick` stays C.
- [ ] 5.17d Device append / sort / query on the 139-bar class. Firmware: IndexCommit appends `tickEvents`, one `isort` slice, Arduino `win` then `prep`. Complete line `iapp=` / `isort=`. C baseline [`155953`](../../../captures/session_20260815_155953.log). Native `commitCapturePass` still fills `byTick`. Device remasure next.
- [ ] 5.17e Swap `TickIndex` representation only if 5.17d PASS. No B. No A2. No `recon`. No `pair`.

## 6. Production swap (only after all three gates + user approval)

- [ ] 6.1 Dirty overdub fallback → `resolveWindow`; keep 3b clean-cache copy
- [ ] 6.2 Idle visual slices gather via `resolveWindow` (range-dirty bars)
- [ ] 6.3 Long-loop playback gather → `resolveWindow` / `ResolvedEvent`
- [ ] 6.4 Short-loop playback / NOTE_EDIT hydrate last
- [ ] 6.5 Do **not** delete `materializeToEventVector`; do **not** call resolution from `handleMidiInput`

## Out of scope

- Persisted D3 checkpoint (`StorageManager`)
- D4 `LoadLoopJob` publication
- Stage 3b GUS replacement
- Overlay picker, interval reservation, RC-J
- Replacing `NoteGeometryResolver`
