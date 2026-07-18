# Deferred storage — time-budgeted scheduler (Phase A)

**Kind:** enhancement  
**Role:** **Phase A** of [`deferred_job_scheduler_architecture.md`](deferred_job_scheduler_architecture.md)  
**Branch / scope:** `feature/deferred-lazy-load`  
**Related:** [`BOOT_LOAD.md`](../Guides/BOOT_LOAD.md), [`PersistenceBudget`](../../include/PersistenceBudget.h), OpenSpec `unified-commit-lazy-slot-load`  
**Evidence:** [`session_20260718_190417.log`](../../captures/session_20260718_190417.log)

---

## Goal (Phase A only)

Replace **byte-budgeted** deferred SD loop load with **time-budgeted** execution of **resumable `LoadLoopJob`**, still owned by `StorageManager`.

This is **not** the final architecture. It introduces Job-shaped work + µs budgets so Phase B can move execution to `DeferredJobScheduler` without rewriting load logic.

Objective: storage work never monopolizes MIDI / playback / UI; focused load gets a larger slice than background; **demote-on-focus** preserves progress.

---

## Locked decisions (from architecture)

| Topic | Decision |
|-------|----------|
| North star | `DeferredJobScheduler` ([DEC-027](../DECISION_LOG.md#dec-027-deferred-job-scheduler-north-star)) |
| Phase A owner | `StorageManager::runDeferredFrame(budgetUs)` |
| Focus change | Demote, do not cancel |
| Commit | Atomic only (`applySnapshot` at job Completed) |
| Preemption policy | Finish if estimated remaining work &lt; slice |
| Job state | Fields on `LoadLoopJob` — no Workspace / Context / Scratch type |
| Byte chunks | Internal to a timed step, not the public API |

### Phase A pinned decisions

- Restore frame executes **before** display.
- Deferred save remains under `StorageManager` / `processDeferredSaveState` during Phase A (separate from load frame).
- `applySnapshot()` may exceed one scheduler slice during MVP — measure and revisit before Phase B.

---

## Design principles (Phase A)

### 1. Time is the scheduling currency

| Today | Phase A |
|-------|---------|
| Read up to N bytes → return | Spend up to N µs → return |

### 2. Main loop

```
MIDI / clock / playback / buttons
    → StorageManager::runDeferredFrame(budgetUs)   // LoadLoopJob
    → display update
    → processDeferredSaveState                    // separate in Phase A
```

### 3. Focus priority + demotion

| Job | Budget |
|-----|--------|
| Focused `LoadLoopJob` | FocusRestoreUs (~1000–1500) |
| Background / demoted load | BackgroundRestoreUs (~250–300) |
| Boot title audible | BootTitleRestoreUs (~3000–8000) |

On focus change: previous load priority → Low; new focus → High; **keep job fields and file position**.

---

## Non-goals (Phase A)

- New `DeferredJobScheduler` type (Phase B)  
- Partial commits / progressive hydration  
- Full display/export job migration  
- Unbounded while-drain under title  

---

## Current codebase anchors

| Piece | Today | Phase A target |
|-------|--------|----------------|
| Load | `processDeferredLoopSlotRestore()` one byte-chunk or apply | `LoadLoopJob::step` inside `runDeferredFrame(budgetUs)` |
| Job state | `LoadLoopJob` + buffer fields | Keep state on the Job |
| Focus gate | `isFocusedLoopSlotRestoreWork` | Drive High vs Low priority / budget |
| Persistence | `processDeferredSaveState` + `PersistenceBudget` | Stays separate in Phase A (`SaveLoopJob` later) |
| Boot 8KB | `setBootSlotRestoreHighThroughput` | Prefer BootTitleRestoreUs |

---

## Phase A work breakdown

### A.1 — Time budget API

```text
StorageManager::runDeferredFrame(uint32_t budgetUs)
// or overload processDeferredLoopSlotRestore(budgetUs) then rename
```

```text
deadline = micros() + budgetUs
while (micros() < deadline && hasRunnableJob)
    selectJob()           // High focus first; preemption policy
    job.stepOnce()        // or small reads until deadline
```

### A.2 — LoadLoopJob owns state

Shape current deferred load as `LoadLoopJob` fields:

- file handle/pos, buffer, bytesRead, payloadSize, parse/apply phase, `SlotLoadSession*`, priority  
- Steps: begin open, timed SD read, parse, applySnapshot (Commit), complete + display invalidate  

**Demote:** keep job instance; only change priority for `selectJob`.

### A.3 — Budget constants

```text
FocusRestoreUs        = 1200
BackgroundRestoreUs   = 250
BootTitleRestoreUs    = 5000
```

Policy from transport state (recording suspends background; playing allows focus High only or demoted Low with small budget).

### A.4 — Preemption policy

Normally High preempts Low. **Exception:** if running job’s estimated remaining work (e.g. unread bytes / apply pending) **&lt; remaining slice**, finish it before switching.

### A.5 — Instrumentation

CAP/PERF: slice duration max/avg, steps, focus vs background, bytes progress. No hot-path Serial spam.

### A.6 — applySnapshot hitch (known)

Document MVP: `applySnapshotToLoop` may exceed FocusRestoreUs once per load. Split or idle-gate in A.6 / Phase B follow-on — required before claiming “focus feels instant” for 64-bar slots ([`190417`](../../captures/session_20260718_190417.log)).

---

## Success criteria (Phase A)

- [x] Public scheduling unit is µs, not KB — `LoadLoopBudget` + `runDeferredFrame`
- [x] Focus load demoted on slot/track change without restarting SD read from 0 — `parkedLoadLoopJob_`
- [x] Paused / demoted jobs resume from previous progress without rereading completed SD data
- [x] Slices respect budget except documented apply one-shot (MVP overshoot logged)
- [ ] No regression vs [`190417`](../../captures/session_20260718_190417.log) play smoothness — **device gate**
- [ ] Tap / MIDI Start still timely — **device gate**
- [x] `pio test -e native`; `teensy41-capture-serial` build

**Shipped (2026-07-18):** Phase A firmware — time budgets, park/demote, load-before-display. Device gate pending.

---

## Pre-implementation review (Phase A)

### Ready

- Extends `StorageManager`; Job vocabulary approved for north star; demote approved  
- Mirrors `PersistenceBudget` patterns  

### Resolved

| Topic | Decision |
|-------|----------|
| North star | `DeferredJobScheduler` / DEC-027 |
| Phase A owner | `StorageManager::runDeferredFrame` |
| Demote vs cancel | Demote |
| Preemption | Remaining work &lt; slice |
| Partial Commit | Forbidden |
| Main order | Load frame **before** display |
| Deferred save | Separate `processDeferredSaveState` in Phase A |
| Apply overshoot | Allowed in MVP; measure before Phase B |

### Proceed?

- **YES** Phase A — pinned decisions locked above.  
- **NO** Phase B `DeferredJobScheduler` until Phase A device gate passes + OpenSpec for new owner.

---

## Implementation notes

- Prefer action+scope: `runDeferredFrame`, `LoadLoopJob` (state on job).  
- Remove `setBootSlotRestoreHighThroughput` when BootTitleRestoreUs ships.  
- Architecture gate Phase A: Ownership **NO**, Transition **NO**, Reuse **YES** — extend StorageManager.
