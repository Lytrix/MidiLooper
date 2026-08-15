## 1. Docs and gates (this session)

- [x] 1.1 Architecture plan — [`loop_event_sourced_resolution_architecture.md`](../../../docs/Plans/loop_event_sourced_resolution_architecture.md)
- [x] 1.2 DEC-037 in DECISION_LOG; NAMING.md vocabulary
- [x] 1.3 Close `loop-effective-event-source` tasks 4.1 / 4.2; update CURRENT_WORK, PROJECT_STATE, DELIVERABLE_TRACKING
- [x] 1.5 Derived-index storage invariant recorded on DEC-037 (not a new DEC): bulk PSRAM arrays; no per-entry associative insert on realtime-adjacent index construction. 5.15–5.18 prove it empirically. `openOnByPitch` LIFO retained.

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

- [x] 5.3 Record worst-case µs, not only totals (native 64-bar fixture: materialize 145 µs, window 28 µs, rebuild 214 µs, `resolveState` 1 µs). Device 139-bar [`173842`](../../../captures/session_20260815_173842.log): `win=7129` `reb=368240` `st=531` `iapp=197743` `isort=28116` `capp=10682` `csort=1897` `bn=225` `nsort=10003`.
- [x] 5.1 **PASS** [`173842`](../../../captures/session_20260815_173842.log): 139-bar idle complete path. OLED consecutive `DFRAME` 1.034 s vs healthy 0.968 s. `midi_gap` 39.1 ms. `idle_maint` 34.9 ms. No `loop_rem`. No `VCACHE,full` during LCR. Production unchanged. Gate does not run while PLAYING.
- [x] 5.2 **PASS** [`180624`](../../../captures/session_20260815_180624.log): PLAYING overdub 139-bar / 2388 notes. `begin_capture` **10050 µs** (bar < 50 ms; prior FAIL [`175544`](../../../captures/session_20260815_175544.log) 108979 µs; 3b [`045556`](../../../captures/session_20260814_045556.log) 2214 µs). No `VCACHE,stale_all` on the scored entry; no `VCACHE,full` in the capture. Overdub `clockrate` 47–48. 3b copy restored. Stage 6: consume-only; 6A → 6B → 6C.

Device probe: sliced `DeviceGateSession` in idle maintenance (`linker/imxrt1062_t41_lcr.ld`). Does **not** consult `MemoryMonitor`. Tick-index / `startsByTick` use `ExternalMemoryFirstAllocator`. Arm waits for any pending slot restore. CrashReport [`115242`](../../../captures/session_20260815_115242.log) `0x6003616C` `_M_emplace_equal` / `0x10`. 30-bar `hist=832` [`112843`](../../../captures/session_20260815_112843.log). >63-bar size [`115750`](../../../captures/session_20260815_115750.log) `hist=2394` `reb=101s`. [`134954`](../../../captures/session_20260815_134954.log) complete `hist=2394`. IndexCommit / pairing / reconstruct span build / display project / RebuildSpans batch `kDeviceGateEventsPerSlice`. Arm cap stays off. Do not persist or put resolution on overdub/MIDI.

- [x] 5.4 Sparse `soundingAt`: keep `spans` + `startsByTick`; device stride **8 bars** (`kDeviceCheckpointBarStride`). Native Stage 7/8 stay at 1 bar. Same `resolveState` answers (`test_stage7_sparse_checkpoints_agree_with_dense`).
- [x] 5.5 Withdrawn: LCR MUST NOT consult advisory pressure. Tick-index / `startsByTick` use `ExternalMemoryFirstAllocator` (CrashReport [`115242`](../../../captures/session_20260815_115242.log) `0x6003616C` `_M_emplace_equal` at `0x10`).
- [x] 5.6 Split `prepareRebuildSpans` into `prepareRebuildResolvedEvents` then `finishRebuildSpansFromEvents` (one idle slice each). Device `RebuildPrepare` / `RebuildSpans`. Native `rebuild()` still composes both.
- [x] 5.7 Representation on the >63-bar idle gate **closed** (5.7c FROZEN [`170024`](../../../captures/session_20260815_170024.log)): `spanBoundaries` / `tickEvents` / channel lookup flat; `walk=0`; no `idle_maint` `loop_rem` on those paths; `chan`/`spans` `DFRAME` 0.933–0.965 s. Pair leftover closed by **5.18b** [`173842`](../../../captures/session_20260815_173842.log).
- [x] 5.7a Native: `appendSpansFromNotes` reserves `2 * notes.size()`; `appendTickEventEntries` reserves remaining events in the pass. Device [`163942`](../../../captures/session_20260815_163942.log) reserve **PASS**.
- [x] 5.7b Native PASS / **device FAIL** [`164922`](../../../captures/session_20260815_164922.log): `channelByNoteId` from every NOTE_ON. Open-note tests PASS. First `spans` slice `idle_maint` **14.7 s**. Do not keep this map on device. Do not rewrite `pair` / `recon`.
- [x] 5.7c **FROZEN** [`170024`](../../../captures/session_20260815_170024.log): flat `{noteId, channel}` append + `stable_sort` + first-wins unique. `capp=12373` `csort=1779`. Do not reopen. Pair is 5.18.
- [x] 5.18 Pair contracts pinned. Plan: [`loop_content_resolution_pair_index_refinement.md`](../../../docs/Plans/loop_content_resolution_pair_index_refinement.md). Do not flatten from container type. No 5.1. No B. No `recon`.
- [x] 5.18a Native: pair walk reports `tot`/`bn`/`op`/`lk`/`oth`, last-wins, peak depth. Device [`172927`](../../../captures/session_20260815_172927.log) **PASS**: `bn=5345535` `op=2433` `lk=1424` `pk=1`. `byNoteId` owns pair. Do not flatten `openOnByPitch`.
- [x] 5.18b **FROZEN** [`173842`](../../../captures/session_20260815_173842.log): last-wins flat `byNoteId` `bn=225` `nsort=10003` (was `bn=5345535`). Pair `DFRAME` 0.980–1.026 s. Do not flatten `openOnByPitch`. No B. No `recon`.
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
- [x] 5.17d Device append / sort / query on the 139-bar class. [`161355`](../../../captures/session_20260815_161355.log) `iapp=4819607` `isort=27415` `win=13971` `st=3108` `walk=0`. No `idx` `loop_rem`. No B. No A2. Native `commitCapturePass` still fills `byTick`.
- [x] 5.17e Drop `byTick` from `commitCapturePass` / `indexCapturePassEventRange`. `findRawWindow` reads `tickEvents` only. Device remasure [`162630`](../../../captures/session_20260815_162630.log) matches [`161355`](../../../captures/session_20260815_161355.log) (`iapp=4816490` `isort=27113` `win=14016` `st=3105` `walk=0`; no `idx` `loop_rem`). **5.17 complete.**

## 6. Production swap (6A / 6B / 6C) + 6D maintenance investigation

- [x] 6.0 Overdub start/stop MUST NOT synchronously construct, sort, checkpoint, or resolve LCR state. Consume already-prepared derived state only. `ensure*` rebuild helpers on that path are violations. LCR is the producer of prepared derived state, not a replacement for `overdubSourceView`. Keep 3b `visualCache.notes` copy. `begin_capture` **< 3 ms** regression target vs 3b [`045556`](../../../captures/session_20260814_045556.log) **2214 µs**; **< 50 ms** hard gate. DEC-037 amendments 2026-08-15.
- [x] 6A Prepared display range (idle): **PASS** [`185931`](../../../captures/session_20260815_185931.log) `DIAG,lcr,6a,win=784,proj=5539,oracle=9192,tot=6323,ev=78,notes=40,match=1`. `midi_gap` max 43.6 ms during LCR (then 14.3 ms). DFRAME consecutive 0.977 s at the 6A sample (LCR idx peak 1.027 s). No `VCACHE,full`. No `loop_rem` during the gate. `walk=0`. Overdub not wired; `begin_capture` 6688 / 8639 / 9250 µs. Do not start 6B until asked.
- [x] 6B Commit invalidation: **PASS** [`192334`](../../../captures/session_20260815_192334.log). Three 139-bar overdub stops: `VCACHE,stale_range` notes **2375 / 2437 / 2450** `dcnt` **15 / 5 / 5** (not 139). No `adopt_partial`. No `VCACHE,full`. No 139-bar `stale_all` on stop (boot-only). `DisplayFullRebuild` stays **5**. `ODUB,stop,display` **35 / 30 / 26 ms**. Idle `slice_clean` **461 / 207 / 158 ms** later notes **2437 / 2450 / 2464**. `PlaybackFullMaterialize` **0**. Contrast [`185931`](../../../captures/session_20260815_185931.log) `adopt_partial` 2403 → 496 / 117 bars dirty.
- [ ] 6C Prepared LCR range → `overdubSourceView`. Native landed. Consume-when-ready only — does not address always-ready after commit. Device recapture optional ([`194643`](../../../captures/session_20260815_194643.log) RING dropped `6c`). Score against **2214 µs** if recaptured.
- [ ] **6D** Incremental post-commit overdub-query index — investigation. **6D.1** one-vector sort/merge **FAIL**. **6D.2** split history+delta **PASS**. **6D.3 native PASS:** N successive overdubs append into delta only; `no_delta` `query_us=4` / 8+0 candidates at H=8192 and 32768 through N=16 (Δ_acc=128); `commit_us=1` at both H. Not live incremental LCR. Next: consider production architecture gate. Production untouched. [`loop_content_resolution_incremental_commit_maintenance_refinement.md`](../../../docs/Plans/loop_content_resolution_incremental_commit_maintenance_refinement.md). A/B **rejected**. 6.0 unchanged.
- [ ] After 6C device score — investigate MIDI Input Gap > 50 ms in [`192334`](../../../captures/session_20260815_192334.log): `DIAG,midi_gap` **135 / 119 / 138 ms** at 54.7 / 64.7 / 69.8 s while `clockrate` stayed **47**. Not a 6B fail. Do not start during 6D. Do not treat as RC-J (that is post-STOPPED 48→24→0).
- [ ] 6.3 Long-loop playback gather → `resolveWindow` / `ResolvedEvent` (after 6C)
- [ ] 6.4 Short-loop playback / NOTE_EDIT hydrate last
- [ ] 6.5 Do **not** delete `materializeToEventVector`; do **not** call resolution from `handleMidiInput`; do **not** remove the 3b copy path

## Out of scope

- Persisted D3 checkpoint (`StorageManager`)
- D4 `LoadLoopJob` publication
- Stage 3b GUS replacement
- Overlay picker, interval reservation, RC-J
- Replacing `NoteGeometryResolver`
