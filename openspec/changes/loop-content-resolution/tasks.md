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

Device probe: sliced `DeviceGateSession` in idle maintenance (`linker/imxrt1062_t41_lcr.ld`). Does **not** consult `MemoryMonitor`. Tick-index / `startsByTick` use `ExternalMemoryFirstAllocator`. Arm waits for any pending slot restore. CrashReport [`115242`](../../../captures/session_20260815_115242.log) `0x6003616C` `_M_emplace_equal` / `0x10`. 30-bar `hist=832` [`112843`](../../../captures/session_20260815_112843.log). >63-bar size [`115750`](../../../captures/session_20260815_115750.log) `hist=2394` `reb=101s`. [`122259`](../../../captures/session_20260815_122259.log) finalized the same 139-bar gate (`reb=100533678`). IndexCommit / RebuildSpans batch `kDeviceGateEventsPerSlice`. Arm cap stays off. Do not persist or put resolution on overdub/MIDI.

- [x] 5.4 Sparse `soundingAt`: keep `spans` + `startsByTick`; device stride **8 bars** (`kDeviceCheckpointBarStride`). Native Stage 7/8 stay at 1 bar. Same `resolveState` answers (`test_stage7_sparse_checkpoints_agree_with_dense`).
- [x] 5.5 Withdrawn: LCR MUST NOT consult advisory pressure. Tick-index / `startsByTick` use `ExternalMemoryFirstAllocator` (CrashReport [`115242`](../../../captures/session_20260815_115242.log) `0x6003616C` `_M_emplace_equal` at `0x10`).
- [x] 5.6 Split `prepareRebuildSpans` into `prepareRebuildResolvedEvents` then `finishRebuildSpansFromEvents` (one idle slice each). Device `RebuildPrepare` / `RebuildSpans`. Native `rebuild()` still composes both.
- [ ] 5.7 Measure selected-loop idle gate on a loop **>63 bars**: `DIAG,lcr` or explicit skip; no `idle_maint` ~50 ms; no 1 s `DFRAME` gaps. Size probe accepted: [`115750`](../../../captures/session_20260815_115750.log) (139 bars / 2393 notes). That capture **FAIL** latency (`idle_maint` 46.3 s / 100.7 s, `reb=101146131`). [`121702`](../../../captures/session_20260815_121702.log) 50 ms yield → one emplace per slice. [`122259`](../../../captures/session_20260815_122259.log) finalized (`reb=100533678`, `hist=2394`); later slot-1 96 notes is idle, not a second LCR probe. Re-measure after 5.11.
- [x] 5.8 [`115750`](../../../captures/session_20260815_115750.log) `st=16249` `rep=350` `hist=2394` `walk=0`. Query path does not need a third index. Stall is cold IndexCommit / rebuild.
- [x] 5.9 Arm cap stays off (2026-08-15). 5.7 probe is >63 bars; that is not a reason to restore the 16-bar cap.
- [x] 5.10 Bound device-gate IndexCommit and RebuildSpans to `kDeviceGateSliceBudgetUs` (50 ms, same as 5.7 `idle_maint`). One event / one span per inner step; yield when the budget is exhausted. Reconstruct stays its own slice. Native `test_stage9_sliced_*` PASS. Cap stays off. Superseded as the slice unit by 5.11 — a single PSRAM emplace already exceeds 50 ms ([`121702`](../../../captures/session_20260815_121702.log)).
- [x] 5.11 IndexCommit / RebuildSpans process `kDeviceGateEventsPerSlice` (8) per idle slice. Rate-limited `DIAG,lcr,phase,<name>,pass,<u>,ev,<u>,span,<u>,notes,<u>` on step change and at most 1 Hz. Native `test_stage9_range_*` / `test_stage9_phase_line_*` PASS. Cap stays off.

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
