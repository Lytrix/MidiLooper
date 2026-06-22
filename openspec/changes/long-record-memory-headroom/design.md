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
Add `PsramFirstAllocator<T>` (sibling of `ExtMemAllocator`, order inverted: `extmem_malloc` first, `malloc` fallback, PSRAM-range-aware `deallocate`). Apply it to the per-loop note cache (`CachedNoteList`), playback order vector, `materializeToFlat` output vectors, and undo snapshot vectors.

**Rationale:** These grow with record length and are read off the hot path (or tolerate PSRAM latency with the 32 KB cache). Moving them frees RAM2 for the hot path and SD library.

**Alternatives considered:**
- Invert `ExtMemAllocator` globally: rejected; would push genuinely hot buffers to slower PSRAM.
- Static `EXTMEM` arrays: rejected; fixed sizing wastes PSRAM and loses per-loop flexibility.

### Decision 2: RAM2 safety-floor admission guard
Extend the admission concept already in `LoopEventStore` (`canAllocChunkWithReserve`) with a `MemoryMonitor::getFreeHeap()` floor. Below the floor, non-time-critical growth and persistence yield (retry next idle); the MIDI/clock/playback path is exempt and never blocked.

**Rationale:** Defense against any future RAM2 regression; turns a silent `abort()` into a deterministic, observable back-off.

**Alternatives considered:**
- Rely solely on PSRAM re-targeting: rejected; no guard if a new RAM2 consumer appears.

### Decision 3: Bounded incremental deferred save (no large RAM2 allocation)
Harden the existing `DeferredSaveStage` machine (`src/StorageManager.cpp`) so every slice — including the undo-stack stage — has a working set no larger than `LoopEventStoreConfig::CHUNK_CAPACITY` and never allocates a full-pass vector. Fix the 48-bar slice path that currently dispatches but never completes. Save runs while playback continues, yielding each loop iteration.

**Rationale:** Persistence is most fragile right after a long stop when RAM2 is most pressured; it must not need a contiguous block then.

**Alternatives considered:**
- Synchronous save on stop: rejected; increases worst-case stop latency on the timing-critical path.

### Decision 4: Time-sensitive data priority is explicit and tested
Memory back-off and deferred save run only from idle/playback context in `main.cpp`, after MIDI/clock/playback servicing, and yield each iteration. No memory walk (`getPsramFreeBytes`/`logStatus`) runs on record/overdub/stop paths (carried from `record-stop-64-bar-crash`).

## Risks / Trade-offs

- **[Risk] PSRAM latency on note cache / playback order** → **Mitigation:** these are not per-sample hot; the 32 KB PSRAM cache covers sequential reads; HITL first-note timing gates catch regressions.
- **[Risk] Re-targeting misses a RAM2 consumer** → **Mitigation:** RAM2 floor guard + native low-heap test + HITL heap-floor gate at 64 bars.
- **[Risk] Bounded save state machine complexity** → **Mitigation:** native test drives a full 64-bar save under forced-low floor; assert max temporary batch ≤ `CHUNK_CAPACITY`.
- **[Risk] Allocator name churn** → **Mitigation:** `PsramFirstAllocator` mirrors existing `ExtMemAllocator`; confirm naming before apply.

## Migration Plan

1. M1: add `PsramFirstAllocator`; re-target note cache, playback order, `materializeToFlat`, undo snapshots; add RAM2 floor guard.
2. M1 verify: native PSRAM-placement + floor tests; HITL 48/64-bar record-only show free RAM2 above floor and `PERS,result,...,ok`.
3. M2: make deferred save fully bounded; fix 48-bar completion.
4. M2 verify: native full-64-bar save under low floor; HITL 64+64 baseline reaches `STOPPED_RECORDING -> PLAYING -> OVERDUBBING` and persists; reload-after-reboot passes.
5. `pio test -e native`; update `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`.
6. Rollback: allocator re-target and floor guard are isolated; revert per-container if a timing regression appears.

## Open Questions

- What RAM2 floor value gives safe headroom for SD-library buffers without rejecting legitimate growth (candidate: derive from observed 16-bar `getFreeHeap` minus SD/library worst case)?
- Should undo snapshot residency move wholesale to PSRAM, or only the length-scaling event/chunk-ref vectors within snapshots?
- Confirm `PsramFirstAllocator` as the allocator name (vs reusing/parameterizing `ExtMemAllocator`).
