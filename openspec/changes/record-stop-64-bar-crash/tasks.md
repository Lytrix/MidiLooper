## 1. Reproduce and isolate stop-path failure

- [ ] 1.1 Reproduce 64+64 baseline failure and store serial/report artifacts with deterministic filenames.
- [x] 1.2 Add stop-path stage markers from record stop entry through seal, publish, finalize, visual-cache request, REVT queue, state advance, and save request.
- [x] 1.3 Stage markers include elapsed microseconds, free heap before/after (`MemoryMonitor::getFreeHeap()` only), event count, chunk ref count where relevant, and outcome.
- [x] 1.4 Stop-path markers MUST NOT call `MemoryMonitor::logStatus()`, `getPsramFreeBytes()`, or `getPsramUsedBytes()` on record-stop or overdub-stop paths.
- [x] 1.5 Update baseline parser to report the first missing stop-path stage and the slowest completed stage explicitly.

## 2. Stop-path state progression hardening

- [x] 2.1 Trace `Track::stopRecordingTrack` and finalize flow for long record passes to find the blocking stage.
- [x] 2.2 Remove display-only visual cache rebuild from synchronous record-stop path; mark visual cache dirty for deferred rebuild.
- [x] 2.3 Move record verification event emission (`REVT` queue/sweep) out of the synchronous record-stop path or process it in bounded slices.
- [x] 2.4 Implement bounded stop finalize behavior so `RECORDING -> STOPPED_RECORDING -> PLAYING` completes for 64-bar runs.
- [ ] 2.5 Ensure overdub start immediately after long record stop enters `PLAYING -> OVERDUBBING` when requested.
- [x] 2.6 Remove `flattenActiveCapturePasses` from `logMemoryAfterOverdubStop`; use O(1) storage metadata for loop buffer location instead.

## 3. Persistence durability on long stop

- [x] 3.1 Trace stop-path persistence handoff (`StorageManager` / `StorageLoopIo`) for long record finalize.
- [x] 3.2 Emit explicit persistence success/failure evidence for long stop-path handling.
- [x] 3.3 Ensure a finalized 64-bar record is reloadable from SD after reboot simulation.
- [x] 3.4 Implement `StorageLoopIo` chunk-stream writer path for capture pass persistence (no full pre-flatten vector; max temporary event batch <= `LoopEventStoreConfig::CHUNK_CAPACITY`).
- [x] 3.5 Add native round-trip test coverage for chunk-stream writer compatibility with existing read path.
- [x] 3.6 Add native assertion/report for max temporary event batch size during 64-bar capture pass persistence.

## 4. Regression tests and HITL gates

- [ ] 4.1 Add native tests for long stop finalize completion and transition to `PLAYING`.
- [ ] 4.2 Add native/host persistence test covering long record stop then reload.
- [ ] 4.3 Extend `host_midi_automation_baseline.py` long-run gates for stop-path stages, transition completion, and persistence evidence.
- [ ] 4.4 Extend baseline report with stop-path time/memory summary (slowest stage, heap deltas, max temporary batch size).
- [ ] 4.5 Run `pio test -e native` and 64+64 HITL baseline; capture pass/fail evidence in change notes.

## 5. Documentation and closeout

- [ ] 5.1 Update `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md` with long stop-path guarantees, diagnostics markers, and chunk-stream writer memory bound.
- [ ] 5.2 Record resolved root cause and mitigation in this change’s `design.md` open questions section.
- [ ] 5.3 Run `openspec validate record-stop-64-bar-crash` and prepare for `/opsx:apply`.

## 6. Display scaling for long loops

- [ ] 6.1 Implement 16-bar cap for detailed piano-roll window when loop length exceeds 16 bars.
- [ ] 6.2 Implement compact full-width overview strip between piano roll and note information (or the lowest piano-roll row if space is insufficient).
- [ ] 6.3 Overview strip v1: binary grouped note presence only (**has notes** / **no notes**) using 1/2/4/8/16/32/64-bar dynamic grouping.
- [ ] 6.4 Overview strip v1: draw current detailed-window start/end marker so the shown piano-roll region is visible in full-loop context.
- [ ] 6.5 Add display verification in HITL capture (`#CAP DISP`) to confirm bounded window + overview behavior on 32/64-bar loops.
- [ ] 6.6 Document future display zoom controls: fader 3 in **LOOP_EDIT** and hold-turn encoder in **LOOP_EDIT** may adjust detailed-window length from 1 to 16 bars; not implemented in this change.
