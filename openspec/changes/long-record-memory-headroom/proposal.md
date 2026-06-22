## Why

Recording past ~32 bars crashes the firmware: USB serial drops and the just-recorded pass is lost. This supersedes `record-stop-64-bar-crash`, which treated the failure as a stop-path timing/persistence bug. Captured evidence shows the real root cause is **RAM2 heap exhaustion that scales with record length**, not stop-path scheduling.

Proof (record-only HITL, no overdub):

- 16-bar record: `MemoryMonitor::getFreeHeap()` reports `77824` bytes at seal; deferred save completes (`#CAP,...,PERS,result,...,ok`).
- 48-bar record (`captures/hitl_record_only_48bar_20260622_203457_serial.log`): `getFreeHeap()` already reports `4096` bytes at `record_stop` entry — before the save runs; `PERS,dispatch` fires, then the device goes silent ~30 ms later. No `PERS,result`.

Mechanism: `ExtMemAllocator::allocate()` (`include/Utils/ExtMemAllocator.h`) calls `malloc()` first and only spills to PSRAM when the RAM2 `malloc` heap is full. `getFreeHeap()` measures exactly that RAM2 heap (`_heap_end - __brkval`). The event chunk pool is already PSRAM-first (`LoopEventStore::poolAlloc` → `extmem_malloc`), so it is not the leak. The RAM2 drain comes from `ExtMemAllocator`-backed, length-scaling containers — per-loop note cache and playback order, `materializeToFlat` temporaries, and undo snapshots — which fill the 512 KB RAM2 heap before the 8 MB PSRAM is touched. By the time SD persistence runs (it also needs RAM2 for SD-library buffers and a write batch) only ~4 KB remains, and the allocator/`abort()` path or a hard fault kills USB.

## What Changes

- Add a PSRAM-first allocator and re-target the length-scaling, non-hot containers (note cache, playback order, `materializeToFlat` temporaries, undo snapshots) so they consume the 8 MB PSRAM instead of the scarce 512 KB RAM2 heap. Keep time-critical and small state in fast RAM.
- Add a RAM2 safety-floor admission guard (extending the existing chunk admission idea in `loop-event-pool-admission`) so growth-heavy and persistence work yields when free RAM2 drops below a configured floor, while the MIDI clock / note-out / playback path is never blocked.
- Harden the deferred save into a fully bounded incremental writer so persistence never needs a large contiguous RAM2 allocation, fixing the 48-bar save that currently dispatches but never reaches `PERS,result`.
- Add native coverage (forced-low free-heap simulation) and HITL gates proving free RAM2 stays above the floor and `PERS,result,...,ok` is reached at 48 and 64 bars.

This change is delivered in two sequenced milestones: M1 = allocation re-targeting + RAM2 floor guard (root-cause fix), M2 = bounded incremental deferred save hardening (defense-in-depth).

## Capabilities

### New Capabilities
- `long-record-memory-headroom`: Long records keep RAM2 headroom by directing length-scaling buffers to PSRAM, enforcing a RAM2 safety floor, and persisting via a bounded incremental writer, so recording well past 32 bars does not crash and the recorded pass survives.

### Related Capabilities (referenced, not redefined here)
- `loop-event-pool-admission`: this change adds a RAM2 free-heap floor alongside the existing chunk/heap admission.
- `storage-loop-io`: this change builds on the chunk-stream writer to make the full deferred save bounded.
- `undo-memory-trim`: undo snapshot residency moves to PSRAM-first under this change.
- `timeline-passes`: materialized-view and note-cache residency change; behavior of `materialize` is unchanged.

## Impact

- Affected firmware: `include/Utils/ExtMemAllocator.h` (new sibling `PsramFirstAllocator`), `include/Loop.h` / `src/Loop.cpp` (note cache, playback order residency), `src/LoopPasses.cpp` (`materializeToFlat` temporaries), `TrackUndo.cpp` (snapshot residency), `LoopEventStore.*` (RAM2 floor admission), `StorageManager.cpp` / `src/main.cpp` (bounded incremental save).
- Affected verification: `scripts/host_midi_automation_baseline.py` heap-floor and `PERS,result` gates; native tests using `MemoryMonitor::setNativeTestFreeHeap`.
- Supersedes: `record-stop-64-bar-crash` (parked). Carried-forward open items: overdub-start-after-long-stop, native + HITL gates, docs closeout. Its shipped pieces (chunk-stream writer, `RECS`/`PERS` instrumentation, reload test) remain in place and are reused.
- Non-goals: long-loop display scaling (16-bar window + overview strip), Jam D13 capture, scene workflow, note-edit UX changes.
- Brownfield references: `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`, `docs/DELIVERABLE_TRACKING.md`.
