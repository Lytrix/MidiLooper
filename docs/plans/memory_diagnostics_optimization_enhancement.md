# Memory diagnostics and optimisation — enhancement

**Kind:** enhancement  
**Date:** 2026-07-06  
**Build env:** `teensy41-capture-serial`  
**Guide:** [`INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md`](../Guides/INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md) · OpenSpec: [`openspec/specs/internal-heap-external-memory-routing/spec.md`](../../openspec/specs/internal-heap-external-memory-routing/spec.md)

## Problem

UIP Phase 5.5 HITL fails on **64-bar** workloads: NOTE_EDIT entry hard-faults when internal heap free drops to ~4 KiB. Cold-buffer routing shipped (`b1260ce`) recovers post-setup headroom on short loops but does not eliminate internal-heap allocation on the NOTE_EDIT open transition itself.

## Core principle

> **Diagnostics explain firmware behaviour; they never create firmware behaviour.**

Diagnostics observe state. They do not own application state, influence scheduling, or become required for firmware correctness.

Memory optimisation is the **first consumer** of the diagnostics platform — not the reason the platform exists.

## Platform shape (Phase 0 — shipped)

| Component | Role |
|-----------|------|
| [`Diagnostics.h`](../../include/Utils/Diagnostics.h) | Facade macros: `DIAG_EVENT`, `DIAG_MEMORY`, `DIAG_COUNTER_INC` |
| [`DiagnosticsTypes.h`](../../include/Utils/DiagnosticsTypes.h) | `DiagTraceRecord`, `DiagContextSnapshot`, `formatVersion` |
| [`DiagnosticsEvents.h`](../../include/Utils/DiagnosticsEvents.h) | Categorised event IDs: `(category << 8) \| localId` |
| [`Diagnostics.cpp`](../../src/Utils/Diagnostics.cpp) | PSRAM last-record slot, ring append, counters |
| [`DebugSessionCapture`](../../include/Utils/DebugSessionCapture.h) | Ring flush as `#CAP,...,DIAG,...`; boot `DIAGCHK` |

**No `DiagnosticsManager`.** Context is derived at append time from `TrackState`, `EditSessionType`, `LooperState`, overlay flags — no parallel FSM.

### Diagnostic levels

| Level | Active when | Records | Heap snapshots |
|-------|-------------|---------|----------------|
| OFF | default non-capture | — | — |
| ERROR | optional | failures + boot DIAGCHK | on ERROR |
| TRACE | `SESSION_CAPTURE=1` (default `DIAG_LEVEL=2`) | binary events | at tagged transitions |
| FULL | `-D DIAG_LEVEL=3` | TRACE + scopes (future) | per scope |

`DIAG_LEVEL` gates **binary trace only** — independent of [`Logger`](../../include/Logger.h) `LogLevel`.

### Serial export format

Ring flush (deferred from main loop):

```
#CAP,<us>,DIAG,<formatVer>,<eventId>,<trackState>,<editSession>,<looperState>,<flags>,<recordFlags>,<heapFree>,<heapUsed>,<extmemFree>,<payload>
```

Post-fault boot checkpoint (immediate):

```
#CAP,<us>,DIAGCHK,<same fields>
```

Parse with:

```bash
.venv/bin/python scripts/parse_diag_trace.py captures/session_*.log
```

### Event categories

| Category | Examples |
|----------|----------|
| Memory | `HeapSnapshot`, `AllocationFailure` |
| Playback | `MergeBegin`, `MergeComplete` |
| Edit | `NoteEditOpenEnter`, `AfterRematerializeEditView`, `AfterEnterDefaultState` |
| Storage | `SaveBegin`, `LoadWorkspace` |
| Display | `VisualCacheRebuild` |
| Validation | `InvariantFailed` (Phase 5) |

### Counters (`DIAG_COUNTER_INC`)

| Counter | Producer |
|---------|----------|
| `Materialize` | `EditManager::openNoteEditSession` |
| `PlaybackMergeRebuild` | `Track::ensurePlaybackWindowBuilt` |
| `VisualCacheRebuild` | `Loop::rebuildVisualCacheFromPasses` |
| `CacheInvalidateBroad` | `EditManager::reopenNoteEditSession` |
| `AllocatorFailure` | admission / pool paths |

## NOTE_EDIT measurement rules (Phase 1)

The 64-bar crash predates diagnostics — root cause is transition allocations in `openNoteEditSession`:

1. `loop.rematerializeEditView` — internal-heap `MidiEventVec` in `LoopPasses::materialize`
2. `enterDefaultNoteEditSessionState` → `filterSelectableDisplayNotes` → full `reconstructDisplayNotes`
3. `reopenNoteEditSession` → broad `track.invalidateCaches()`

| Inside `openNoteEditSession` | Allowed | Forbidden |
|------------------------------|---------|-----------|
| Inner steps | `DIAG_EVENT(id)` only (~fixed-size PSRAM write) | `logStatus`, `getLargestFreeBlock`, `Serial.printf` |
| Heap read | At most one at enter **or** exit | Per-step `DIAG_MEMORY` |
| Crash tier | Overwrite `DiagLastRecord` (last event ID wins) | Ring contention with allocation |

### Bisect fallback

If trace instrumentation changes crash behaviour, rebuild with `-D NOTE_EDIT_OPEN_BISECT_STAGE=N` (0–4) in `platformio.ini` `teensy41-capture-serial` `build_flags`. Each stage early-returns with **zero** heap reads inside the open path.

| Stage | Stops after |
|-------|-------------|
| 0 | `rematerializeEditView` |
| 1 | `assignMissingNoteIds` |
| 2 | `discardFlatCache` / `resetNoteEditSessionState` |
| 3 | `enterDefaultNoteEditSessionState` |
| 4 | full open (default) |

## Controlled capture protocol (Phase 1)

1. Fresh power-on → boot `DIAGCHK` if prior fault
2. After `setup()` — `DIAG_MEMORY(HeapSnapshot)` at end of setup
3. 64-bar record + overdub (canonical HITL or UIP matrix)
4. NOTE_EDIT enter
5. Stop capture; parse DIAG timeline + `DIAGCHK`

Pair with existing heap protocol:

```bash
.venv/bin/python scripts/parse_memory_capture.py captures/session_*.log
.venv/bin/python scripts/parse_diag_trace.py captures/session_*.log
```

## Runtime success metrics (HITL gates — Phase 5)

| Metric | Target (64-bar) |
|--------|-----------------|
| NOTE_EDIT open internal allocations | 0 new `malloc`/`vector` growth on transition (extmem OK) |
| Heap below reserve | never on NOTE_EDIT open |
| Allocator failures | `AllocatorFailure` counter == 0 |
| Cache rebuild budget | `VisualCacheRebuild` ≤ 1 per NOTE_EDIT open; merge ≤ 1 |
| Invariant suite | all `RuntimeInvariant` checks pass post-open |

Interim heap budget:

| Gate | Interim |
|------|---------|
| Post-setup internal free | ≥ 120 KiB |
| Post-NOTE_EDIT-open free | ≥ 48 KiB |

## Execution order (locked)

1. **Phase 0** — diagnostics platform (this doc + native 472/472)
2. **Phase 1** — 64-bar repro + evidence report (no optimisation until complete)
3. **Phase 2** — tactical fixes (evidence-driven)
4. **Phase 3** — design session + consolidate derived ownership
5. **Phase 5** — HITL metrics + `RuntimeInvariant` validators
6. **Phase 4** — PSRAM routing audit (`scripts/audit_vector_allocators.py`)

**Do not** lower `HEAP_RESERVE_BYTES` to pass tests.

## Phase 2 tactical fixes (after Phase 1 evidence)

| Priority | Fix | File |
|----------|-----|------|
| 1 | Route `LoopPasses::materialize` through `SessionMidiEventVec` | `LoopPasses.cpp` |
| 2 | Fix by-value copy in `enterDefaultNoteEditSessionState` | `EditManager.cpp` |
| 3 | Reusable workspace for `filterSelectableDisplayNotes` | `NoteEditFocus.cpp` |
| 4 | Boot `reserve()` for 64-bar sizes | `main.cpp`, `MemoryPool.cpp` |

## Phase 1 exit gate

Written evidence report:

- Last `DIAG` / `DIAGCHK` event ID before fault
- `heapFree` at fault
- Counter summary (`Materialize`, `VisualCacheRebuild`, `PlaybackMergeRebuild`)
- Recommended Phase 2 fix order

## Related

- [`capture_serial_ram1_recovery_extmem_debug_enhancement.md`](capture_serial_ram1_recovery_extmem_debug_enhancement.md) — PSRAM ring baseline
- [`internal_heap_psram_routing_refinement.md`](internal_heap_psram_routing_refinement.md) — cold-buffer routing
- [`scripts/parse_diag_trace.py`](../../scripts/parse_diag_trace.py)
- [`scripts/audit_vector_allocators.py`](../../scripts/audit_vector_allocators.py)
