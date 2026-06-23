## Context

Two 64+64 baseline runs (`20260622_025621`, `20260622_030010`) reproduce the same failure:

- `RECORDING -> STOPPED_RECORDING` occurs
- `STOPPED_RECORDING -> PLAYING` does not occur
- overdub start then times out, and the run aborts with `midi clock missing before overdub phase`
- serial capture drops (`Device not configured`) before stop-path completion evidence is emitted

The failure appears in the record stop path, not in short runs (2-bar edit baseline passes). Existing docs in `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md` define stop-path constraints but do not yet gate this long-run failure explicitly.

Primary files for this change: `Track.cpp`, `TrackStateMachine.cpp`, `Loop.cpp`, `StorageManager.cpp`, `StorageLoopIo.cpp`, `scripts/host_midi_automation_baseline.py`.

Hot-path constraint: no full-loop flatten or expensive memory walks in timing-critical capture/stop ISR-adjacent paths.

## Goals / Non-Goals

**Goals:**
- Identify the exact stop-path stage that fails for 64-bar record stop.
- Add deterministic evidence from record stop through finalize, state transition, and persistence completion.
- Ensure long record stop reaches `PLAYING` and allows overdub start.
- Ensure long stop persists data so reboot reload shows the finalized loop.
- Add native and HITL regression gates for this failure mode.

**Non-Goals:**
- Jam D13 capture behavior, scene management, or arrangement workflow.
- Broad note-edit UX changes unrelated to long record stop.
- Reworking pool architecture or undo semantics outside stop-path needs.
- Interactive display zoom controls in this change. Proposed future controls: fader 3 in **LOOP_EDIT** for detailed-window length (minimum 1 bar, maximum 16 bars), plus hold-turn encoder in **LOOP_EDIT** as an alternate zoom control.

## Decisions

### Decision 1: Instrument the stop-path with explicit stage markers
Add stage-level markers around record stop finalize and persistence boundaries (enter finalize, finalize done, publish done, state advance, persistence request/result).

**Rationale:** Current evidence shows transition gaps, but not which stop stage stalls first.

**Alternatives considered:**
- Rely on existing `ST` transitions only: rejected; insufficient granularity.
- Add only script-side retries/timeouts: rejected; hides firmware root cause.

### Decision 2: Add long-run HITL gate for complete stop-path sequence
Extend `host_midi_automation_baseline.py` long-run checks to require:
- `RECORDING -> STOPPED_RECORDING -> PLAYING`
- stop finalize evidence
- persistence evidence (or explicit persist failure evidence)

**Rationale:** Prevents regressions where short runs pass but long stop hangs.

**Alternatives considered:**
- Keep gate only on transitions: rejected; misses persistence stage failures.
- Manual validation only: rejected; not repeatable.

### Decision 3: Keep stop-path work bounded and defer heavy diagnostics
Any extra diagnostics on stop path use O(1) counters/flags and lightweight marker lines; heavier dumps stay outside timing-critical regions.

**Rationale:** Avoid introducing new timing regressions while debugging timing-sensitive code.

**Alternatives considered:**
- Full event dump at stop: rejected; too heavy for long loops.

**Telemetry contract:** each stop-path stage marker records stage name, elapsed microseconds, free heap before/after (`MemoryMonitor::getFreeHeap()` only), event count, chunk ref count where relevant, and outcome. The marker never includes full MIDI event payloads.

**Forbidden on stop path:** `MemoryMonitor::logStatus()`, `getPsramFreeBytes()`, and `getPsramUsedBytes()` — these walk the PSRAM pool via `sm_malloc_stats_pool` and can stall for hundreds of ms. Full PSRAM stats remain in idle-only `main.cpp` periodic logging, same as today.

### Decision 4: Verify persistence with reload-focused checks
Add test coverage that validates 64-bar stop produces persisted data that reloads correctly after restart simulation.

**Rationale:** User-reported failure includes “not stored on SD after crash”; transition-only fixes are incomplete.

**Alternatives considered:**
- Assume persistence if stop reaches `PLAYING`: rejected; does not prove SD durability.

### Decision 5: Add chunk-stream writer for capture pass persistence
Implement a writer path in `StorageLoopIo` that writes capture pass payloads by chunk stream instead of first flattening all events into one `MidiEventVec`.

**Rationale:** Current save path flattens chunk refs into a temporary full vector before write; long loops increase peak memory and stop-adjacent save cost.

**Memory bound:** v1 writer processes at most one `LoopEventStoreConfig::CHUNK_CAPACITY` batch at a time for capture pass payloads. Native tests assert compatibility with the existing read path and report max temporary batch size.

**Alternatives considered:**
- Keep flatten-then-write: rejected; unnecessary peak memory for long capture passes.
- Make save synchronous on stop without changing writer: rejected; increases worst-case stop latency.

### Decision 6: Bound long-loop display work with 16-bar window + overview strip
Display will render at most a 16-bar piano-roll window while showing a compact full-loop overview strip across display width. The strip sits between the piano roll and the bottom information rows if the existing vertical gap is sufficient; otherwise it reuses the lowest row directly below the piano roll.

The overview strip uses dynamic grouping (1, 2, 4, 8, 16, 32, 64 bars) based on total loop length. V1 is binary only: a segment is drawn as **has notes** or **no notes**. The strip also draws a start/end marker for the detailed piano-roll window so the user can see which section of the full loop is currently shown.

**Rationale:** Long loops should remain readable without requiring full-loop per-frame note reconstruction in the main piano-roll layer.

**Alternatives considered:**
- Render all bars directly in piano roll: rejected; scales poorly with long loops.
- Fixed grouping resolution only: rejected; does not scale across short and long loops.
- Note-count density in v1: rejected for this change; binary presence is cheaper and enough for navigation.

## Risks / Trade-offs

- **[Risk] Added instrumentation changes timing** -> **Mitigation:** keep markers lightweight and behind capture-serial diagnostics path; heap fields use `getFreeHeap()` only; never call PSRAM pool-walk APIs on stop path.
- **[Risk] Existing overdub-stop memory log flattens passes** -> **Mitigation:** task 2.6 removes `flattenActiveCapturePasses` from `logMemoryAfterOverdubStop`.
- **[Risk] Fix addresses transition but not persistence durability** -> **Mitigation:** add explicit storage verification gate and reload test.
- **[Risk] Intermittent USB disconnect obscures root cause** -> **Mitigation:** require stage marker ordering so last successful stage is always known.
- **[Risk] Long-run tests are slow** -> **Mitigation:** keep one canonical 64+64 gate and targeted native tests for fast iteration.
- **[Risk] Overview strip bins hide exact note density** -> **Mitigation:** v1 intentionally shows only presence/position; detailed notes remain in 16-bar main window.
- **[Risk] Chunk-stream writer diverges from existing wire layout** -> **Mitigation:** preserve on-wire format and validate round-trip with existing `readPersistedCapturePassWire`.

## Migration Plan

1. Add stop-path stage markers and baseline script parsing for those markers.
2. Reproduce failure and identify first missing stage consistently.
3. Move display-only rebuilds and record verification event emission out of the synchronous record-stop path.
4. Implement chunk-stream writer for capture pass persistence.
5. Implement any remaining firmware fix at the first failing stage boundary.
6. Add native test coverage for stop finalize, bounded persistence, and reload outcomes.
7. Run `pio test -e native`.
8. Run 64+64 HITL baseline and verify transition + persistence gates pass.
9. Keep rollback simple: remove/disable markers and revert stop-path change if long-run reliability regresses.
10. Land chunk-stream writer and bounded display window in isolated commits for clean rollback.

## Open Questions

- Is the first failing stage in finalize compute, state-advance scheduling, or persistence dispatch?
- Should persistence be synchronous at this specific long-stop boundary, or remain deferred with stronger completion checks?
- Which marker set should be treated as contract for future long-run diagnostics (`ST` + new stop stages + storage result)?
- Does the current gap between piano roll and note information fit the strip cleanly on hardware, or does the strip need to steal one row from the piano-roll area?
- Future: should fader 3 in **LOOP_EDIT** and hold-turn encoder in **LOOP_EDIT** adjust detailed-window length from 1 to 16 bars?
