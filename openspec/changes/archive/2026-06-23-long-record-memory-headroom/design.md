## Context

`record-stop-64-bar-crash` instrumented the stop path and added a chunk-stream writer, but HITL evidence proved the crash is RAM2 heap exhaustion that scales with record length, present before the save runs:

- 16-bar record-only: `getFreeHeap = 77824` at seal, `PERS,result,...,ok`.
- 48-bar record-only (`captures/hitl_record_only_48bar_20260622_203457_serial.log`): `getFreeHeap = 4096` at `record_stop` entry, `PERS,dispatch` fires, device silent ~30 ms later, no `PERS,result`.

Allocation policy is the cause:

```86:95:include/Utils/ExtMemAllocator.h
    T* allocate(std::size_t n) {
        const std::size_t bytes = n * sizeof(T);
        // 1. Try fast internal RAM first.
        void* ptr = malloc(bytes);
        if (ptr) return static_cast<T*>(ptr);
        // 2. Internal RAM exhausted: spill over to PSRAM.
        ptr = extmem_malloc(bytes);
```

`getFreeHeap()` measures that same RAM2 `malloc` heap (`_heap_end - __brkval`, `src/Utils/MemoryMonitor.cpp`). The chunk event pool is already PSRAM-first (`src/LoopEventStore.cpp` `poolAlloc`), so the leak is the other `ExtMemAllocator`-backed, length-scaling containers.

Hot-path constraint (repo rule): MIDI clock, note-out, and playback reads must never block or allocate. Persistence and validation always yield to time-sensitive data.

## Goals / Non-Goals

**Goals:**
- Keep free RAM2 above a safety floor through 64-bar record and stop, by directing length-scaling buffers to PSRAM.
- Never block the MIDI/clock/playback path for memory or persistence work.
- Make deferred save fully bounded so it completes without a large RAM2 allocation.
- Prove the fix with native low-heap simulation and 48/64-bar HITL gates.

**Non-Goals:**
- Long-loop display scaling (deferred; tracked separately).
- Jam D13 capture, scenes, note-edit UX.
- Changing the on-SD wire format or `LoopPasses::materialize` semantics.

## Decisions

### Decision 1: PSRAM-first allocator for length-scaling, non-hot buffers
Add `PsramFirstAllocator<T>` (sibling of `ExtMemAllocator`, order inverted: `extmem_malloc` first, `malloc` fallback, PSRAM-range-aware `deallocate`). Apply it to the per-loop note cache (`CachedNoteList`), playback order vector, `materializeToFlat` output vectors, undo snapshot vectors, and display note storage (`VisualCache.notes`, `CapturePreview.notes`, `DisplayManager::liveDisplayNotes`).

**Rationale:** These grow with record length and are read off the hot path (or tolerate PSRAM latency with the 32 KB cache). Moving them frees RAM2 for the hot path and SD library.

**Alternatives considered:**
- Invert `ExtMemAllocator` globally: rejected; would push genuinely hot buffers to slower PSRAM.
- Static `EXTMEM` arrays: rejected; fixed sizing wastes PSRAM and loses per-loop flexibility.
- Rebuild live record display from the capture store every frame: rejected; it reintroduces length-scaling work during recording. The display now reads the incrementally maintained `CapturePreview.notes` path.

### Decision 2: RAM2 safety-floor admission guard
Extend the admission concept already in `LoopEventStore` (`canAllocChunkWithReserve`) with a `MemoryMonitor::getFreeHeap()` floor. Below the floor, non-time-critical growth and persistence yield (retry next idle); the MIDI/clock/playback path is exempt and never blocked.

**Rationale:** Defense against any future RAM2 regression; turns a silent `abort()` into a deterministic, observable back-off.

**Alternatives considered:**
- Rely solely on PSRAM re-targeting: rejected; no guard if a new RAM2 consumer appears.

### Decision 3: Central deferred runtime save path
Harden the existing `DeferredSaveStage` machine (`src/StorageManager.cpp`) into the only runtime persistence path. Runtime actions request a save; they do not call synchronous `saveState()` directly. Each `processDeferredSaveState()` call advances at most one bounded writer step after MIDI clock, note-out, playback, record, and overdub servicing in `main.cpp`.

The writer keeps the existing v4 snapshot format for this change. Every slice — including global header, track metadata, slot metadata, loop pool entries, capture-pass chunks, footer, and undo-stack rows — must return to the main loop without relying on an internal `yield()` as the timing guarantee. OLED display updates are skipped while a deferred save is active; display refresh is non-timing work and must not compete with SD persistence.

**Rationale:** Persistence is most fragile right after a long stop when RAM2 and SD-library state are most pressured. The 2026-06-22 HITL reruns showed RAM2 was healthy (`record_stop` heap `135168`, minimum observed RAM2 `77824`) while the board still reset after `PERS,dispatch`. Diagnostic telemetry in `captures/session_20260622_225653.log` reached only two early `track_header_slots` slices before USB reconnect, proving the remaining failure is save scheduling/architecture rather than another RAM2-only allocation.

**Alternatives considered:**
- Synchronous save on stop/runtime actions: rejected; increases worst-case latency on the timing-critical path and leaves direct `saveState()` call sites outside the bounded writer.
- Journal-style append/replay persistence: deferred to a later OpenSpec change; it is the better long-term persistence architecture, but changing the on-SD model would expand this reliability fix beyond the current v4 snapshot gate.

### Decision 4: Time-sensitive data priority is explicit and tested
Memory back-off and deferred save run only from main-loop background context in `main.cpp`, after MIDI/clock/playback servicing. Capture-active states do not advance persistence slices. No memory walk (`getPsramFreeBytes`/`logStatus`) runs on record/overdub/stop paths (carried from `record-stop-64-bar-crash`).

## Risks / Trade-offs

- **[Risk] PSRAM latency on note cache / playback order** → **Mitigation:** these are not per-sample hot; the 32 KB PSRAM cache covers sequential reads; HITL first-note timing gates catch regressions.
- **[Risk] Re-targeting misses a RAM2 consumer** → **Mitigation:** RAM2 floor guard + native low-heap test + HITL heap-floor gate at 64 bars.
- **[Risk] Bounded save state machine complexity** → **Mitigation:** native test drives a full 64-bar save under forced-low floor; assert max temporary batch ≤ `CHUNK_CAPACITY`; HITL `PERS` stage/cursor telemetry identifies reset boundaries while the gate is open.
- **[Risk] Partial v4 file after reset mid-save** → **Mitigation:** treat missing `PERS,result,...,ok` as failed persistence in HITL. If centralized slicing still exposes partial-file load risk, add a minimal completion marker or temp-write commit step within this change; defer full journaling.
- **[Risk] Allocator name churn** → **Mitigation:** `PsramFirstAllocator` mirrors existing `ExtMemAllocator`; confirm naming before apply.

## Migration Plan

1. M1: add `PsramFirstAllocator`; re-target note cache, playback order, `materializeToFlat`, undo snapshots; add RAM2 floor guard.
2. M1 verify: native PSRAM-placement + floor tests; HITL 48/64-bar record-only show free RAM2 above floor and `PERS,result,...,ok`.
3. M2: centralize runtime save requests through the deferred writer; make every save section fully bounded; fix 48-bar completion.
4. M2 verify: native full-64-bar save under low floor; HITL 64+64 baseline reaches `STOPPED_RECORDING -> PLAYING -> OVERDUBBING` and persists; reload-after-reboot passes.
5. `pio test -e native`; update `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`.
6. Rollback: allocator re-target and floor guard are isolated; revert per-container if a timing regression appears.

## Verification Evidence

- 2026-06-22: `pio test -e native -f test_storage_loop_io` passed with `test_64_bar_save_completes_at_ram2_floor_with_bounded_batch`, confirming 64-bar save completion at the RAM2 floor and max temporary capture-pass batch `<= LoopEventStoreConfig::CHUNK_CAPACITY`.
- 2026-06-22: `pio test -e native` passed (`146 test cases: 146 succeeded`), including `test_pool_budget` RAM2-floor guards, PSRAM placement for display note storage, and `test_storage_loop_io` long-record persistence coverage.
- 2026-06-22 HITL after display/allocator fixes: 48-bar record-only kept RAM2 above the floor (`record_stop` heap `135168`, min observed RAM2 `77824`) but reset after `PERS,dispatch`. Diagnostic slice telemetry reached `track_header_slots` through track 0 slot 1 before reconnect, proving the remaining failure is runtime save architecture.

## Open Questions Resolution

- **RAM2 floor value:** set to `LoopEventStoreConfig::RAM2_SAFETY_FLOOR_BYTES = 12 * 1024`; non-critical deferred save and autosave work defer below this floor while MIDI clock, note-out, and playback stay ungated.
- **Undo residency scope:** length-scaling undo structures are PSRAM-first (`UndoEntryVec` via `PsramFirstAllocator`), preserving bounded RAM2 usage without changing undo semantics.
- **Allocator naming:** `PsramFirstAllocator` is the adopted name and is applied to the M1 target buffers.
