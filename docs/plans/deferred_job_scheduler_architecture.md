# Deferred job scheduler

**Kind:** architecture enhancement  
**Status:** **Approved north star** (2026-07-18) — Phase A firmware may proceed; Phase B introduces `DeferredJobScheduler`  
**Phase A implementation plan:** [`deferred_storage_time_budget_scheduler_enhancement.md`](deferred_storage_time_budget_scheduler_enhancement.md)  
**Branch context:** `feature/deferred-lazy-load` and follow-ons  
**Evidence:** [`session_20260718_190417.log`](../../captures/session_20260718_190417.log)  
**Decision:** [DEC-027](../DECISION_LOG.md#dec-027-deferred-job-scheduler-north-star)

---

## Decision summary

| Topic | Decision |
|-------|----------|
| Architecture | Adopt **`DeferredJobScheduler`** as the long-term owner of non-realtime deferred work |
| First implementation | **Phase A** — time-budgeted `LoadLoopJob` under `StorageManager::runDeferredFrame()` |
| Execution ownership | Migrate from `StorageManager` → `DeferredJobScheduler` in **Phase B** |
| Focus change | **Demote** jobs — do not cancel |
| Commit model | Atomic Commit unchanged (`HEADER_READY` → job state → Commit → published) |
| Job temporary state | Fields **on the Job** — no Workspace / Context / Scratch type |
| Memory policy | Discard **paused** jobs under memory pressure; recreate from published state if still needed |
| Scheduling | Dynamic priority + fairness (remaining work, age, FIFO) |
| Preemption policy | Prefer **estimated remaining work &lt; one slice** over percentage thresholds |
| Vocabulary | See [Final vocabulary](#final-vocabulary) |

---

## Phase A pinned decisions

These are architecture boundaries for the first implementation, not builder footnotes.

- Restore frame executes **before** display.
- Deferred save remains under `StorageManager` / `processDeferredSaveState` during Phase A (not yet a shared `runDeferredFrame` deadline with load).
- `applySnapshot()` may exceed one scheduler slice during MVP. Measure and revisit before Phase B.

Detail plan: [`deferred_storage_time_budget_scheduler_enhancement.md`](deferred_storage_time_budget_scheduler_enhancement.md).

---

## Summary

The **time-budgeted storage path** remains the correct **first implementation step**, but it is **Phase A of this architecture**, not the final design.

North star: **`DeferredJobScheduler`** owns execution of all non-realtime, interruptible, resumable firmware work while preserving real-time guarantees and the immutable commit model.

Domain managers (`StorageManager`, `DisplayManager`, …) **submit and own job logic**; they do not each invent a separate scheduler.

```
Domain Manager
        │
   creates Job
        │
        ▼
DeferredJobScheduler
        │
   executes Job
```

New deferred operations should integrate by implementing new **Job** types rather than introducing additional schedulers or execution loops. Execution policy remains centralized.

---

## Motivation

Deferred activities already share the same shape:

- Lazy loop load (`LoadLoopJob`)  
- Deferred persistence  
- Display cache / geometry  
- Future: MIDI export, waveform, indexing, undo trim  

All are: expensive, interruptible, non-real-time, resumable.

Centralize **execution**; keep **domain logic** in managers.

---

## Architectural layers

```
Realtime:  MIDI · Playback · Clock · UI / encoders
                │
                ▼
        DeferredJobScheduler   (Phase B; Phase A = StorageManager::runDeferredFrame)
                │
                ▼
        Jobs (LoadLoopJob, SaveLoopJob, DisplayCacheJob, …)
                │
                ▼
        Domain targets: Track Slots · Storage · Display · Editor
```

---

## Migration strategy

### Phase A — Low risk (current ownership)

```
Main loop
    → StorageManager::runDeferredFrame(budgetUs)
        → LoadLoopJob step(s)
    → display update
    → processDeferredSaveState   // stays separate in Phase A
```

Goals:

- Resumable jobs (`LoadLoopJob` owns its execution state)  
- Microsecond budgets  
- Demote-on-focus for in-flight load  
- No new top-level scheduler type yet  

See Phase A detail plan: [`deferred_storage_time_budget_scheduler_enhancement.md`](deferred_storage_time_budget_scheduler_enhancement.md).

**Architecture gate Phase A:** ownership change **NO** (still `StorageManager`); scheduling behavior only.

### Phase B — Introduce `DeferredJobScheduler`

```
Main loop
    → DeferredJobScheduler::runFrame(budgetUs)
        → LoadLoopJob | SaveLoopJob | DisplayCacheJob | …
```

Execution ownership moves to the scheduler. `StorageManager` **submits** load/save jobs only.

**Architecture gate Phase B:** formal trigger — new owner; requires DEC + OpenSpec before firmware.

### Phase C — Migrate remaining deferred systems

Piano roll / visual cache, MIDI export, undo trim, background optimization, metadata rebuild — as Idle/Low jobs.

---

## Scheduler responsibilities (Phase B+)

Owns: job ordering, time budgets, priority updates, fairness, memory-aware discard of **paused** jobs.

Does **not** understand storage formats, display geometry, or persistence schemas — only Jobs.

Does **not** own: MIDI timing, playback engine, clock generation, immediate UI handling.

---

## Scheduler invariants

The scheduler guarantees:

- Published state is never partially modified.
- Jobs execute only at scheduler boundaries.
- Every job is either resumable or safely discardable.
- MIDI, playback, and clock execution remain outside scheduler control.
- Job execution state lifetime never exceeds job lifetime.
- Job execution order never bypasses scheduler policy.

---

## Job contract

Every job owns its own execution state: file position, load phase, parser progress, chunk progress, priority, completion/publish logic.

Must: bounded work; no indefinite block; advance one logical step per invocation; safely resumable.

Acceptable: one SD record/sector, one decode/parse block, one persistence writer step, one display segment.  
Unacceptable: entire loop load, full export, full piano-roll rebuild in one step.

No separate Workspace / Context / Scratch object for job temp state. A separate type is justified only if it represents real **execution capacity** (e.g. future `StorageExecutionResource`), not temporary variables.

---

## Slot vs Job

A **Slot** is a playable loop location (Track → Slot 0..7). It is not an execution resource or job container.

```
Track 1 Slot 3 selected
        → LoadLoopJob (process)
        → Track 1 Slot 3 populated (published target)
```

The Job owns progress and unpublished load state. The Slot owns published loop data and user-visible identity.

---

## Focus changes

**Not** cancel. Demote previous High → Low; new focus → High.

Demotion preserves completed SD reads, parser progress, and job fields, avoiding unnecessary reloading after rapid focus changes.

Cancellation only for: memory pressure discard, SD failure, user unload slot, set closed.

---

## Preemption policy

Normally, a newly promoted High-priority job preempts lower-priority work.

**Exception:** if the currently running job has less than one scheduler slice of estimated work remaining, allow it to complete before switching.

Avoid percentage-complete thresholds; use estimated remaining work.

---

## Scheduling policy

1. Priority  
2. User focus  
3. Estimated remaining work  
4. Waiting time  
5. FIFO  

---

## Memory pressure

Affects **paused** jobs first:

Normal: pause → resume later.  
Low: discard lowest-priority **paused** job → recover → recreate the job from published state if it is still required.

Published state is always authoritative. Job execution state is disposable with the job.

Do not starve recording / interactive focus reclaim paths.

---

## Time budgets

| Policy | Example |
|--------|---------|
| Interactive / focus | ~1000 µs |
| Background | ~300 µs |
| Recording | ~100 µs or suspend non-critical |

Selected from firmware state (same idea as `PersistenceBudget`).

---

## Commit boundary

Visible states remain:

```
HEADER_READY → (job execution state) → COMMITTED
```

Partial load data stays inside the Job until Commit. Playback, editor, persistence, and display stay consistent.

---

## Final vocabulary

| Concept | Name |
|---------|------|
| Project/edit environment | **Workspace** |
| Playable loop location | **Slot** |
| Deferred operation | **Job** |
| Scheduler owner | **DeferredJobScheduler** |
| User workflow | **Session** (product sense; keep existing `*Session` types) |
| Event reaction | **Handler** |
| Domain owner | **Manager** |

Avoid for job temp / scheduler resources: Workspace, Scratch, Context, Slot (unless matching the product meanings above).

---

## Formal triggers

| Phase | Trigger? |
|-------|----------|
| A | **No** — extend `StorageManager` |
| B | **Yes** — new `DeferredJobScheduler` owner; DEC + OpenSpec before code |

---

## Success criteria (north star)

- [ ] Focus switch demotes in-flight `LoadLoopJob`; no full re-read of same slot  
- [ ] Paused jobs resume from previous progress without rereading completed SD data  
- [ ] Single atomic Commit at job completion  
- [ ] `runFrame` / `runDeferredFrame` slices within budget except documented one-shots until apply is split  
- [ ] MIDI timing stable under concurrent deferred work  
- [ ] Pressure can discard paused Low `LoadLoopJob` without touching published loops  
- [ ] Native tests: demote, preemption exception, cancel destroys job state  

---

## Recommendation (locked)

1. **North star** = this document.  
2. **Implement next** = Phase A per time-budget plan (`LoadLoopJob` + µs under `runDeferredFrame`).  
3. **Then** Phase B `DeferredJobScheduler` extraction when load (+ save) jobs are proven atomic and demote-safe.
