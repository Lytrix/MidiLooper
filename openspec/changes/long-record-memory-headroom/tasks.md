## 1. Milestone 1 — allocation re-targeting + RAM2 floor guard

- [ ] 1.1 Add `PsramFirstAllocator<T>` in `include/Utils/` (`extmem_malloc` first, `malloc` fallback, PSRAM-range-aware `deallocate`), reusing the address-range logic in `src/LoopEventStore.cpp`.
- [ ] 1.2 Re-target per-loop note cache (`CachedNoteList`) and playback order vector (`include/Loop.h`) to `PsramFirstAllocator`.
- [ ] 1.3 Re-target `materializeToFlat` output `MidiEventVec` temporaries (`src/LoopPasses.cpp`) so long-pass materialization does not draw RAM2 first.
- [ ] 1.4 Re-target undo snapshot length-scaling vectors (`TrackUndo.cpp`) to PSRAM-first.
- [ ] 1.5 Add a RAM2 safety-floor admission guard (extend `LoopEventStore` admission with a `MemoryMonitor::getFreeHeap()` floor) consulted before growth-heavy and persistence work; exempt the MIDI/clock/playback path.
- [ ] 1.6 Native test: simulate low free heap (`MemoryMonitor::setNativeTestFreeHeap`) and assert re-targeted buffers report PSRAM placement (`isInPsram`) and the floor guard defers non-critical growth.
- [ ] 1.7 HITL gate: 48-bar and 64-bar record-only keep `getFreeHeap` above the floor at `record_stop` entry and emit `PERS,result,...,ok`.

## 2. Milestone 2 — bounded incremental deferred save

- [ ] 2.1 Make every `DeferredSaveStage` slice in `src/StorageManager.cpp` bounded to ≤ `LoopEventStoreConfig::CHUNK_CAPACITY` working set, including the undo-stack stage (no full-pass `MidiEventVec`).
- [ ] 2.2 Fix the 48-bar deferred-save path that dispatches (`PERS,dispatch`) but never reaches `PERS,result`; ensure it completes while playback runs and yields each loop iteration.
- [ ] 2.3 Confirm `processDeferredSaveState` placement in `src/main.cpp` runs after MIDI/clock/playback servicing and never on the record/overdub critical path.
- [ ] 2.4 Native test: full 64-bar save runs to completion under a forced-low RAM2 floor; assert max temporary event batch ≤ `CHUNK_CAPACITY`.
- [ ] 2.5 HITL gate: 64+64 baseline reaches `STOPPED_RECORDING -> PLAYING -> OVERDUBBING` and `PERS,result,...,ok`; reload-after-reboot test passes.

## 3. Verification and HITL gates

- [ ] 3.1 Extend `scripts/host_midi_automation_baseline.py` with a free-RAM2 floor assertion at stop and a `PERS,result,...,ok` requirement for long runs.
- [ ] 3.2 Extend baseline report with min free RAM2 during the run and the save outcome stage.
- [ ] 3.3 Run `pio test -e native` (full matrix) and capture pass/fail evidence in change notes.
- [ ] 3.4 Run 48/64-bar record-only and 64+64 HITL baselines per `.cursor/rules/HITL-Test-Flow.mdc`; store deterministic capture artifacts.

## 4. Carried forward from superseded `record-stop-64-bar-crash`

- [ ] 4.1 Ensure overdub start immediately after long record stop enters `PLAYING -> OVERDUBBING` when requested.
- [ ] 4.2 Keep the chunk-stream writer, `RECS`/`PERS` stop-path instrumentation, and 64-bar reload test as the baseline this change builds on (no regression).

## 5. Documentation and closeout

- [ ] 5.1 Update `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md` with the RAM2 headroom policy, PSRAM-first buffer list, and bounded-save memory bound.
- [ ] 5.2 Record resolved root cause (RAM2 exhaustion via `ExtMemAllocator` malloc-first) and mitigation in this change's `design.md` open-questions resolution.
- [ ] 5.3 Run `openspec validate long-record-memory-headroom` and prepare for `/opsx:apply`.
