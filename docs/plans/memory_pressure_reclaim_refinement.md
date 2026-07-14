---
name: memory pressure reclaim
openspec_change: runtime-derived-representation-heap
overview: MemoryPressureLevel advisory FSM + Low derived-cache reclaim (1A/1B shipped). Phase 2 pass-metadata extmem PARKED (003306/004415). Next — heap restore via LoopPool extmem + persist backlog; no typedef tier moves.
todos:
  - id: phase-1a-pressure-state-machine
    content: "Phase 1A — MemoryPressureLevel + thresholds + hysteresis + transition DIAG only; no reclaim; native threshold tests"
    status: completed
  - id: phase-1b-low-reclaim
    content: "Phase 1B — tryReclaimDerivedViewCachesUnderPressure at Low+; owner try* APIs; background-first; stale AND not-referenced"
    status: completed
  - id: phase-2a-pass-metadata-extmem
    content: "Phase 2A — PARKED: extmem typedef routing reverted (003306 FATAL, 004415 edit sluggish + overdub reboot)"
    status: cancelled
  - id: phase-2b-loop-pool-extmem
    content: "Phase 2B — LoopPool loops_ in extmem (~34 KiB fixed internal heap gain)"
    status: completed
  - id: phase-2c-critical-reclaim
    content: "Phase 2C — DEFERRED: Critical orchestration declined; heap-restore track instead"
    status: cancelled
  - id: phase-2-heap-restore
    content: "Heap restore — 2B LoopPool extmem + persist backlog drain; no pass/edit typedef moves"
    status: pending
  - id: phase-3-replace-scattered-thresholds
    content: "After reclaim validated — route optional work through pressure level; remove duplicate heap checks"
    status: pending
  - id: phase-4-m6-closeout
    content: "Idle defer reintro; persistence_rows.py; manual gate 215312; OpenSpec archive"
    status: pending
  - id: manual-gate-215312
    content: "Re-run 64-bar + 7-track overdub; observable success criteria (0 append failed, rebuild OK)"
    status: pending
isProject: false
---

# Memory pressure reclaim — refinement

**OpenSpec:** [`runtime-derived-representation-heap`](../../openspec/changes/runtime-derived-representation-heap/) · **M6 follow-on**  
**Predecessor (merged to `dev`):** [`multi_track_playback_pressure_closure_refinement.md`](multi_track_playback_pressure_closure_refinement.md) — Ph 1–2 + record-start headroom  
**Evidence:** [`session_20260714_215312.log`](../../captures/session_20260714_215312.log) — 64-bar record, ~148 s overdub, 7 tracks playing; no crash; 42× `Capture append failed`; heap 114688 → 49152; M6 display ~10:1 incremental

**Branch:** `feature/memory-pressure-reclaim`

**Session conclusions:** [`memory_pressure_reclaim_conclusions_refinement.md`](memory_pressure_reclaim_conclusions_refinement.md) (2026-07-15)

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

---

## Architecture principles

| Principle | Rule |
|-----------|------|
| **Advisory, not authoritative** | `MemoryPressureLevel` **expresses system pressure** — it does not command subsystems. Each owner decides whether a reclaim action is **safe** at that level (`try*` / guard-and-skip), same as today. |
| **Pressure computation** | `MemoryMonitor::resolveMemoryPressureLevel()` once per main-loop turn — **signal only** |
| **Reclaim ownership stays local** | `Loop`, `Track`, `StorageManager`, `DisplayManager` retain eligibility and safety checks |
| **Event-driven reclaim** | On pressure-level transition or revision-stale **while already at Low+** |
| **Not periodic** | No every-N-bars eviction |
| **Incremental rollout** | Validate state machine before reclaim; validate reclaim before threshold refactors |
| **Rollback-friendly** | Keep `MemoryPressureLevel` even if an individual reclaim strategy is disabled |

**Advisory flow (not a central command bus):**

```text
MemoryMonitor  →  MemoryPressureLevel  (advisory signal)
                         ↓
              subsystems consult level
                         ↓
              each owner: tryReclaim / defer optional work
              (owner decides if safe — may no-op)
```

---

## Design — `MemoryPressureLevel`

**Signal owner:** `MemoryMonitor` (compute once per main-loop turn).  
**Consumers (advisory):** `TrackManager`, `StorageManager`, `DisplayManager`, `PassReclaim` — **consult** level when choosing optional work or reclaim; each subsystem **may no-op** if unsafe. Do not scatter raw `if (heap < …)` until Phase 3.

```cpp
enum class MemoryPressureLevel : uint8_t {
  Normal,
  Low,
  Critical,
};
```

### Level intent (behavioral, not threshold-only)

| Level | Intent |
|-------|--------|
| **Normal** | Full functionality — no compromises |
| **Low** | Reclaim **rebuildable derived caches** only; preserve capture, playback, selected-track UX |
| **Critical** | Preserve **capture and playback** before optional responsiveness; pass chunk reclaim + aggressive persist drain (**no** undo trim — pass undo is O(1)) |

### Inputs (two axes → one level)

| Signal | API |
|--------|-----|
| Internal heap tail free | `MemoryMonitor::getInternalHeapFreeBytes()` |
| Chunk pool | `LoopEventStore::freeChunkCount()` vs `PassConfig::CHUNK_RESERVE` |
| Persist backlog | `PersistenceQueue::queueDepth()` (telemetry + Critical enter) |

### Thresholds (initial calibration — configuration, not architecture)

Values below are **starting points for Phase 1A**; expected to change based on HITL and manual capture sessions. They belong in `Config` / `Globals.h`, not in normative spec prose.

| Level | Enter (initial) | Exit / hysteresis (initial) |
|-------|-----------------|-----------------------------|
| **Normal** | heap ≥ 64 KiB **and** chunks free > reserve + margin | — |
| **Low** | heap 32–64 KiB **or** rising chunk pressure | Normal only when heap ≥ 80 KiB stable ≥ 500 ms |
| **Critical** | heap < 32 KiB (`HEAP_RESERVE_BYTES`) **or** `freeChunkCount() ≤ CHUNK_RESERVE` **or** capture append failure latch | Low when heap ≥ 48 KiB **and** chunks free > reserve + margin |

Add `Config::HEAP_PRESSURE_*` constants in `Globals.h`; tune without changing subsystem ownership or advisory contract.

Append-failure hook: latch Critical until chunks drain or timeout (calibrate in Phase 1A manual session).

---

## Transition telemetry

Emit on **level change only** — negligible RAM1. Include context for correlating heap vs chunk pool vs persist backlog:

```text
#CAP,DIAG,pressure,Normal->Low,63488,19,0
#CAP,DIAG,pressure,Low->Critical,28672,4,18
#CAP,DIAG,pressure,Critical->Low,49152,34,2
```

Fields: `transition`, `heap_free`, `chunks_free`, `persist_queue_depth`.

Phase 1A deliverable: correct transitions `Normal → Low → Critical` (and recovery) with **no reclaim side effects**.

---

## Policy table

| Level | Derived caches | Undo | Visuals (optional) | Persistence |
|-------|----------------|------|--------------------|-------------|
| **Normal** | Keep all | Normal depth | Full display / LED / REVT | Respect existing admission gates |
| **Low** | Reclaim rebuildable caches (background-first — see below) | No trim | Selected-track display; defer non-selected `visualCache` | Continue mid_pass; existing gates |
| **Critical** | Low actions + `reclaimUnreferencedDisabledPasses()` | **No trim** (pass undo already O(1); ClearSlot snapshots pinned until pass reclaim) | Skip REVT; defer LED merge; non-selected visual idle off | **Override non-critical gating** after reclaim (see below) |

### Reclaim priority order (Low+)

Reclaim **background before user-visible**. M6 selected-track ownership rules remain preferred behavior.

1. **Non-selected tracks** — `!TrackManager::isSelectedTrack(track)`
2. **Non-armed tracks** — not `isArmed()` / not capture-active on that track
3. **Recently inactive tracks** — not PLAYING/OVERDUBBING/RECORDING
4. **Selected track** — only if still required after steps 1–3 (heap still Low+)

### Reclaim eligibility (strengthened)

Revision-stale alone is **not sufficient**.

```text
Eligible for reclaim when:
  revision stale (builtFromRevision != loop.playbackRevision)
  AND
  not currently referenced by an active consumer
```

**Not currently referenced** means, per cache type:

| Cache | Not referenced when |
|-------|---------------------|
| Playback window | Track not in hot playback path this tick **or** window already stale vs `playbackRevision` **and** track is background per priority order |
| Materialized store | No open NOTE_EDIT; no in-flight stop-path materialize; playback uses chunk merge |
| `visualCache` | Non-selected loop **or** display path can stale-while-revalidate |
| `publishedMidiScratch_` | Not in NOTE_EDIT |

Ownership must stay explicit — revision numbers are a hint, not sole authority.

### Low — reclaim hooks (Phase 1B; try-reclaim on owner)

Pressure **suggests** reclaim; the owner **decides**. Prefer `try*` / bool-return APIs over blind discard:

| Advisory call | Owner decides safety |
|---------------|----------------------|
| `Track::tryReleasePlaybackWindowMemory()` | Background tracks first; selected last; skip if window still referenced this tick |
| `Loop::tryDiscardPassesMaterializedCache()` | No edit session; not in stop-path materialize; not referenced |
| `Track::tryClearPublishedMidiScratch()` | Not in NOTE_EDIT |

**Avoid:** `if (pressure >= Low) loop.discardPassesMaterializedCache()` with no guard inside `Loop`.

**Prefer:** `if (pressure >= Low) loop.tryDiscardPassesMaterializedCache()` — returns false when unsafe.

### Critical — persistence (wording)

**Do not** describe as “ignoring internal heap safety floor.”

```text
Normal
    StorageManager respects existing admission gates
    (including hasInternalHeapHeadroomForNonCriticalWork).

Critical
    StorageManager MAY tryOverrideNonCriticalPersistAdmission()
    after other subsystems have tried reclaim in the same turn —
    only if its own safety checks pass.

NOTE_EDIT
    Continues to obey HEAP_RESERVE_BYTES regardless of pressure level.
```

Order in Critical: **reclaim first** → then allow mid_pass / work-item slices below 12 KiB non-critical floor. Wire `PersistenceQueue::queueDepth()` into backpressure (M6 Option B).

---

## Call order (main loop)

```text
pressure = MemoryMonitor::resolveMemoryPressureLevel()   // Phase 1A — signal only

// Phase 1B+ — orchestration consults level; each track/loop may no-op
if (pressure >= Low)
  trackManager.tryReclaimDerivedViewCachesUnderPressure(pressure)

// Phase 2+
if (pressure >= Critical)
  trackManager.tryReclaimUnderCriticalPressure()

// Phase 2+ — StorageManager consults level; owns persist admission override
StorageManager::processDeferredSaveState(..., pressure)
```

`TrackManager` **coordinates** background-first order and passes `pressure` — it does **not** bypass owner safety checks.

Reclaim on **level transition** or **eligible stale cache while already at Low+** — never on a bar timer.

---

## What levels will not restore

| Target | Recoverable? |
|--------|--------------|
| Boot ~120+ KiB tail free | No — only clear slot / reboot |
| Post–64-bar publish ~112 KiB | No — owned pass state |
| Reported tail-free after reclaim | Partially — high-water break may not move |

**Do not** use reported heap alone as success signal (see exit criteria).

---

## Implementation phases (incremental)

### Phase 1A — Pressure state machine only

**No reclaim behavior.** Observable output = correct level transitions only.

- `MemoryPressureLevel` + thresholds + hysteresis in `MemoryMonitor`
- Transition telemetry (`#CAP,DIAG,pressure,...`)
- Native: threshold edges, hysteresis, append-failure latch
- Manual: run 215312-style session; confirm `Normal → Low → Critical` in log without behavior change

**Gate:** transitions correct before Phase 1B merge.

### Phase 1B — Enable Low-level reclaim

After 1A validated:

- `TrackManager::tryReclaimDerivedViewCachesUnderPressure(Low)` — orchestration only
- Owner `try*` methods on `Loop` / `Track` (materialized discard, playback window, scratch)
- Background-first priority order; stale **and** not-referenced inside each owner
- Main-loop hook on level transition + eligible stale while Low+

**Gate:** no playback glitches / dropped MIDI / stale display on selected track; rebuild after reclaim OK.

### Phase 2 — Heap restore (revised 2026-07-15)

**Pass metadata extmem is PARKED** — blanket 2A (`003306`) and split-tier 2A (`004415`, edit sluggish) reverted (`896c70d`). **Phase 2C never shipped; deferred.**

**Detail:** [`memory_pressure_phase2_pass_metadata_extmem_refinement.md`](memory_pressure_phase2_pass_metadata_extmem_refinement.md)

#### Reported heap vs usable heap

`getInternalHeapFreeBytes()` = tail above `__brkval` (high-water break). Freed blocks are **reusable** but the metric often **does not rise** until reboot or slot clear. Gate on append failures and stop publish, not tail-free alone.

#### Heap-restore levers (no typedef / tier moves on pass or edit vectors)

| Lever | Expected effect | UX risk |
|-------|-----------------|---------|
| **2B LoopPool extmem** | ~34 KiB fixed off internal heap at pool init | Low |
| **Persist backlog drain** | 004415 Critical with `queue_depth=42`; starves capture | Medium (SD I/O) |
| **Phase 1B Low reclaim** | Already shipped; PSRAM derived views | Low |
| **Idle pass chunk reclaim** | Existing `reclaimUnreferencedDisabledPasses` | Low |
| **Fresh workspace boot** | 57 KiB vs 86 KiB post-setup (004415 vs 235620) | Manual |

**Do not pursue:** extmem routing for `ChunkIdList`, `EditPassVec`, or other pass/edit typedefs.

**Next implement:** **2B LoopPool extmem**, then persist queue investigation.

### Phase 3 — Replace scattered heap checks

**Only after Phase 1B–2 validated.**

1. Route optional work through `MemoryPressureLevel`
2. Remove duplicate threshold comparisons
3. `overUndoMemoryPressure` delegates to level ≥ Critical (trim remains opportunistic on push — not Critical orchestration)

Keeps debugging simple: behavior proven before gate refactors.

### Phase 4 — M6 closeout

- Re-introduce idle defer (non-selected only; `isSelectedTrack` guard)
- Ship `persistence_rows.py`
- OpenSpec archive when exit criteria met

---

## Success criteria (observable behavior)

Prefer **user-visible outcomes** over reported heap values:

| Criterion | Gate |
|-----------|------|
| No `Capture append failed` | 215312 config |
| No allocator failures | `AllocatorFailure=0` in DIAG snapshot |
| Capture completes | Record + overdub stop publish OK |
| Heap trend | No monotonic decrease across **repeated** overdub sessions (same set loaded) |
| Rebuild after reclaim | Playback + display correct after Low reclaim |
| Hot-path counters | `PlaybackFullMaterialize=0`, `LegacyMidiEvents=0` |
| Pressure telemetry | Transitions logged with heap/chunks/queue |
| NOTE_EDIT | Admission still respects `HEAP_RESERVE_BYTES` |
| Native | `pio test -e native` |

---

## Rollback criteria

If reclaim introduces:

- playback glitches
- dropped MIDI events
- stale display state on selected track
- editor inconsistencies
- incorrect rebuild behavior

Then:

1. **Keep** `MemoryPressureLevel` (Phase 1A stays enabled)
2. **Disable** the affected reclaim strategy (feature flag or early return in `reclaimDerivedViewCachesUnderPressure`)
3. Investigate ownership / eligibility before re-enabling

The pressure framework remains valid even when a single reclaim action is reverted.

---

## Pre-implementation review

### Ready

- M6 Ph 1–2 on `dev` (`11025ca`)
- Policy table + level intent agreed
- Incremental phases (1A / 1B) reduce regression risk
- Reclaim targets traced to existing owners
- Evidence log and heap semantics documented

### Open before coding

1. Threshold bytes — initial `Config` values; recalibrate in Phase 1A manual session (not architecture)
2. Append-failure Critical latch duration
3. Per-cache “not currently referenced” predicates — document in code at reclaim site

### Proceed?

**YES** — implement **Phase 1A only** first on `feature/memory-pressure-reclaim`.
