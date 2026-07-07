## Why

64+64 HITL on track 2 / slot 1 (`captures/host_midi_automation_baseline_20260707_192649.json`) shows save **starvation** during overdub: `requestDeferredSaveState` after record stop, then `isCaptureActiveForPersistence()` blocks **all** deferred-save slices for the entire overdub (~128 s). Chunk pool and internal heap fill with no SD drain. Overdub stop faults before `PERS,result,...,ok` (serial ends after `ODUB,stop,set_state`).

Heap routing fixes (`runtime-derived-representation-heap` M1–M3) improved seal heap (53,248 bytes) and core transitions but do not fix the architectural gap: persistence is **transport-gated** and **pass-close-gated**, not bandwidth-limited. The SD writer already streams one chunk per finite-state-machine slice; the gap is **when** it may run.

**Park** further transport-gated persistence patches and stop-path workarounds (`SC_REC_FLUSH` defer, etc.). This change supersedes persistence starvation fixes on `runtime-derived-representation-heap`.

Agent guide: [`docs/Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md`](../../../docs/Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md).

## Core architectural invariants

1. **Runtime ownership is independent of persistence state.**
2. **A sealed chunk is immutable.**
3. **Persistence is cooperative and budget-driven.**
4. **Persistence preserves chunk seal order.**
5. **Runtime playback and recording always take precedence over persistence.**
6. **Memory reclamation is independent of persistence completion.**

**Governing rule:** Runtime recording and playback correctness always take precedence over persistence progress.

## What Changes

- **Pass ownership ≠ storage ownership** — a capture pass may remain open while sealed chunks within it are persisted.
- **Separate lifecycles** — runtime (`Free → Recording → Sealed`) vs persistence (`Not scheduled → Queued → Writing → Persisted`).
- **ChunkManager** (`LoopEventStore`, target) owns sealing, reference tracking, allocation, reclamation — not SD writes.
- **Persistence queue** — sealed chunks admitted in seal order, exactly once; drained in seal order.
- **Cooperative budget-driven scheduler** — replace transport hard block with bounded slices that yield each main-loop iteration; scheduler work follows queue availability (Phase 3 after Phase 2).
- **Mid-pass persistence** — chunk sealed at capacity (and other triggers) during capture; writer drains queue while pass open.
- **Recovery** — load reconstructs the longest valid prefix of successfully persisted sealed chunks.
- **Diagnostics-first** — Phase 0 telemetry (`oldestDirtyChunkAge`, queue depth, seal→persist latency) before behavior change.

## Scope (v1)

> Continuous runtime persistence applies only to **append-only capture-pass** storage.

> Edit passes, undo snapshots, and other mutable runtime structures continue using the existing deferred persistence model unless explicitly extended by a future proposal.

**Note edit** (`editPasses[]`, `NoteEditSession`) is explicitly out of scope.

## What Stays Unchanged

- `Loop`, `Capture`, `LoopPasses`, materialized store, undo copy-on-write semantics
- Deterministic MIDI timing — writer never runs unbounded in one main-loop turn
- Main-loop priority: clock → MIDI → playback → buttons → display → persistence slice if budget remains

## Storage format

Initial implementation SHALL reuse the current persistence format where practical. Alternative storage layouts remain valid provided they satisfy persistence, ownership, and recovery invariants. OpenSpec specifies **behavior**, not a mandated on-disk layout.

## Capabilities

### New Capabilities

- `runtime-invariants` — six core invariants + governing rule; normative ownership boundaries
- `chunk-manager` — runtime chunk lifecycle, sealing, reference tracking, reclamation
- `persistence-lifecycle` — queue states, seal-order admission and drain, exactly-once enqueue
- `persistence-scheduler` — cooperative budget-driven slices; runtime precedence
- `persistence-diagnostics` — Phase 0 starvation and backlog telemetry
- `persistence-failure-policy` — defined behavior under memory pressure

### Modified Capabilities

- `long-record-memory-headroom` — persistence progress during capture; 64+64 `PERS,result` without USB disconnect

## Impact

- Firmware: `LoopEventStore`, `Loop.cpp` (capture tail seal), `StorageManager`, `StorageLoopIo`, `PersistenceBudget`, `main.cpp`
- Guides: [`RUNTIME_STORAGE_AND_PERSISTENCE.md`](../../../docs/Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md) (authoritative map); update [`DEFERRED_RUNTIME_PERSISTENCE.md`](../../../docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md) after Phase 3 ships
- Verification: `pio test -e native`; Phase 0 64+64 HITL diagnostics; Phase 6 full gate
- Relationship: complements `runtime-derived-representation-heap` (heap routing); supersedes persistence/stop-path patches there

## Out of Scope (v1)

- DMA SPI offload (secondary CPU optimization)
- Jam / Scene persistence
- Edit-pass or undo-snapshot continuous persistence
- Lowering `INTERNAL_HEAP_SAFETY_FLOOR_BYTES` without bounded SD proof
