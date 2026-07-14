---
name: memory pressure reclaim
openspec_change: runtime-derived-representation-heap
overview: Formal MemoryPressureLevel policy (Normal / Low / Critical) with ownership-driven derived-cache reclaim, undo trim, and persistence backpressure. Follow-on to M6 Ph 1–2 merged to dev 2026-07-14.
todos:
  - id: pressure-level-core
    content: "MemoryMonitor::resolveMemoryPressureLevel + hysteresis; Config thresholds; emit #CAP,DIAG,pressure on level change only"
    status: pending
  - id: low-reclaim-derived
    content: "TrackManager::reclaimDerivedViewCachesUnderPressure(Low+) — playback windows, discardPassesMaterializedCache, revision-stale first; not periodic"
    status: pending
  - id: critical-reclaim
    content: "Critical path — trimGlobalUndoStackForMemory, reclaimUnreferencedDisabledPasses, disable optional visuals (REVT, non-selected visualCache, LED merge defer)"
    status: pending
  - id: critical-persistence-inversion
    content: "At Critical — prioritize mid_pass / work-item drain over INTERNAL_HEAP_SAFETY_FLOOR block; raise slice budget; wire queue depth into backpressure"
    status: pending
  - id: replace-scattered-thresholds
    content: "Route optional-work gates through pressure level; keep HEAP_RESERVE_BYTES for NOTE_EDIT admission only"
    status: pending
  - id: native-tests
    content: "test_memory_pressure_level — threshold edges, hysteresis, policy table hooks (native mocks)"
    status: pending
  - id: manual-gate-215312
    content: "Re-run 64-bar record + 7-track overdub (session_20260714_215312 config); 0 Capture append failed; level transitions logged"
    status: pending
  - id: m6-phase3-idle-defer
    content: "Re-introduce non-selected idle defer only after pressure policy lands — guard with isSelectedTrack"
    status: pending
  - id: phase4-closeout
    content: "Ship persistence_rows.py + M6 OpenSpec archive when exit criteria met"
    status: pending
isProject: false
---

# Memory pressure reclaim — refinement

**OpenSpec:** [`runtime-derived-representation-heap`](../../openspec/changes/runtime-derived-representation-heap/) · **M6 follow-on**  
**Predecessor (merged to `dev`):** [`multi_track_playback_pressure_closure_refinement.md`](multi_track_playback_pressure_closure_refinement.md) — Ph 1–2 + record-start headroom  
**Evidence:** [`session_20260714_215312.log`](../../captures/session_20260714_215312.log) — 64-bar record, ~148 s overdub, 7 tracks playing; no crash; 42× `Capture append failed`; heap 114688 → 49152; M6 display ~10:1 incremental

---

## Problem

After M6 Ph 1–2, hot-path eager materialize is gone (`PlaybackFullMaterialize=0`, `LegacyMidiEvents=0`), but **long multi-track capture still exhausts headroom**:

| Signal | Session 215312 |
|--------|----------------|
| Internal heap tail free | 114688 → 49152 (~66 KB drop during overdub) |
| Capture append failures | 42 (chunk pool / memory pressure) |
| Post-publish recovery | Heap stays at 49152 — publish commits passes, does not drop derived caches |

**Root cause (four buckets):**

1. **Legitimately owned** — ~25 KB step at record seal (published pass + chunk index metadata). Expected; not reclaimable without losing the loop.
2. **Caches intentionally retained** — playback windows (7 tracks), materialized store, visualCache, display merge buffers. **Primary reclaim target.**
3. **High-water heap metric** — `getInternalHeapFreeBytes()` = tail above `__brkval`; freed cache blocks may be reusable but not visible in the metric.
4. **Leak** — no evidence in 215312 (flat heap after stop, AllocatorFailure=0).

**Not periodic eviction.** Reclaim is **ownership-driven**: pressure level + revision-stale caches.

---

## Design — `MemoryPressureLevel`

**Owner:** `MemoryMonitor` (compute once per main-loop turn).  
**Consumers:** `TrackManager`, `StorageManager`, `DisplayManager`, `PassReclaim` — query level; do not scatter `if (heap < …)`.

```cpp
enum class MemoryPressureLevel : uint8_t {
  Normal,
  Low,
  Critical,
};
```

### Inputs (two axes → one level)

| Signal | API |
|--------|-----|
| Internal heap tail free | `MemoryMonitor::getInternalHeapFreeBytes()` |
| Chunk pool | `LoopEventStore::freeChunkCount()` vs `PassConfig::CHUNK_RESERVE` |
| Persist backlog (optional) | `PersistenceQueue::queueDepth()` |

### Thresholds (initial — tune on device)

| Level | Enter | Exit (hysteresis) |
|-------|-------|-------------------|
| **Normal** | heap ≥ 64 KiB **and** chunks free > reserve + margin | — |
| **Low** | heap 32–64 KiB **or** rising chunk pressure | Normal only when heap ≥ 80 KiB stable ≥ 500 ms |
| **Critical** | heap < 32 KiB (`HEAP_RESERVE_BYTES`) **or** `freeChunkCount() ≤ CHUNK_RESERVE` **or** capture append failure hook | Low when heap ≥ 48 KiB **and** chunks free > reserve + margin |

Add `Config::HEAP_PRESSURE_LOW_BYTES`, `HEAP_PRESSURE_NORMAL_BYTES`, `HEAP_PRESSURE_HYSTERESIS_MS` in `Globals.h`.

**Telemetry:** `#CAP,DIAG,pressure,<0|1|2>` on **level change only** (negligible RAM1).

---

## Policy table

| Level | Derived caches | Undo | Visuals (optional) | Persistence |
|-------|----------------|------|--------------------|-------------|
| **Normal** | Keep all | Normal depth | Full display / LED / REVT | Current `maxPersistenceMicrosActive` |
| **Low** | Drop **playback windows** (all tracks; revision-stale first); **discard materialized store** where no edit session | No trim | Selected-track display only; defer non-selected `visualCache` rebuild | Continue mid_pass — do **not** block on 12 KiB floor if chunks are starved |
| **Critical** | Low actions + `reclaimUnreferencedDisabledPasses()` | `trimGlobalUndoStackForMemory` per track (respect `MIN_UNDO_DEPTH`) | Skip REVT slices; defer LED merge; skip non-selected visual idle | **Aggressive drain** — raise slice budget; prioritize mid_pass over optional work |

### Low — reclaim hooks (existing symbols)

| Action | Owner |
|--------|-------|
| `Track::releasePlaybackWindowMemory()` | All tracks when level ≥ Low (not only at capture start) |
| Revision-stale window | Clear when `builtFromRevision != loop.playbackRevision` |
| `Loop::discardPassesMaterializedCache()` | Loops without active NOTE_EDIT |
| `Track::publishedMidiScratch_.clear()` | When not in NOTE_EDIT |

### Critical — persistence inversion

Today: `hasInternalHeapHeadroomForNonCriticalWork` (**12 KiB floor**) **blocks** mid_pass when heap is low — worsens chunk pool starvation.

At **Critical:** allow mid_pass / work-item slices even below 12 KiB tail free (capture append failure is the trigger). Compensate by dropping caches and trimming undo first in the same loop turn.

Wire `PersistenceQueue::queueDepth()` into `hasDeferredSaveWork()` or a sibling `hasPersistenceBackpressure()` (M6 Option B).

**Invariant:** `HEAP_RESERVE_BYTES` (32 KiB) remains the **hard floor for NOTE_EDIT admission** — unchanged regardless of level.

---

## Call order (main loop)

```text
pressure = MemoryMonitor::resolveMemoryPressureLevel()

if (pressure >= Low)
  trackManager.reclaimDerivedViewCachesUnderPressure(pressure)

if (pressure >= Critical)
  trackManager.reclaimUnderCriticalPressure()

StorageManager::processDeferredSaveState(..., pressure)  // slice budget from level
// DisplayManager / idle maintenance read pressure for optional paths
```

Reclaim on **level transition** or when caches become revision-stale while already at Low+ — **not** every N bars.

---

## What levels will not restore

| Target | Recoverable? |
|--------|--------------|
| Boot ~120+ KiB tail free | No — only clear slot / reboot |
| Post–64-bar publish ~112 KiB | No — owned pass state |
| Telemetry “heap free” after reclaim | Partially — high-water break may not move; success = **0 append failures** + stable capture |

---

## Implementation phases

### Phase 1 — Level core + Low reclaim

- `MemoryPressureLevel` + hysteresis in `MemoryMonitor`
- `TrackManager::reclaimDerivedViewCachesUnderPressure`
- Main-loop hook; DIAG pressure on change
- Native: threshold / hysteresis tests

### Phase 2 — Critical + persistence inversion

- Undo trim + pass reclaim at Critical
- Persistence slice budget + mid_pass admission flip
- Optional visual deferrals
- Native: `test_persistence_failure_policy` extensions if needed

### Phase 3 — Replace scattered thresholds

- `overUndoMemoryPressure` delegates to level ≥ Critical
- Optional work (`emitStoredMidiVerification`, REVT, idle visual) gated by level
- Remove duplicate heap checks where level suffices

### Phase 4 — M6 closeout

- Re-introduce Phase 3 idle defer (non-selected only) with `isSelectedTrack` guard
- Manual gate 215312 config; ship `persistence_rows.py`
- OpenSpec archive when exit criteria met

---

## Exit criteria

| Criterion | Gate |
|-----------|------|
| 64-bar record + 7-track overdub | No crash; **0** `Capture append failed` |
| Hot-path counters | `PlaybackFullMaterialize=0`, `LegacyMidiEvents=0` during stress |
| Pressure telemetry | `#CAP,DIAG,pressure` shows Low during overdub; Critical only under provoked starvation |
| Native | `pio test -e native` |
| NOTE_EDIT | Admission still respects `HEAP_RESERVE_BYTES` |

---

## Pre-implementation review

### Ready

- M6 Ph 1–2 on `dev` (`11025ca`)
- Policy table agreed (Normal / Low / Critical)
- Reclaim targets traced to existing owners
- Evidence log and heap semantics documented

### Open before coding

1. Exact threshold bytes — calibrate from 215312 + one Critical repro session
2. Whether append-failure hook latches Critical until chunks drain (recommended: yes, with timeout)

### Proceed?

**YES** — implement Phase 1 on branch `feature/memory-pressure-reclaim`.
