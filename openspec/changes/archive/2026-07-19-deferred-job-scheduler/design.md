## Context

Phase A shipped time-budgeted `LoadLoopJob` under `StorageManager::runDeferredFrame` with demote-on-focus, atomic Commit, parse slicing, and PLAYING Low-skip ([`230145`](../../../captures/session_20260718_230145.log)). DEC-027 Phase B moves **execution ownership** to `DeferredJobScheduler` while domain managers keep job logic and Commit.

North star: [`deferred_job_scheduler_architecture.md`](../../../docs/Plans/deferred_job_scheduler_architecture.md).

## Goals / Non-Goals

**Goals:**

- `DeferredJobScheduler::runFrame(budgetUs)` is the sole main-loop entry for deferred **load** job stepping.
- `StorageManager` submits `LoadLoopJob` work and implements step/Commit; it does not own the frame loop long-term.
- Preserve Phase A realtime policies and Commit immutability.
- Behavior-preserving migration path (thin adapter first, then real queue).

**Non-Goals:**

- `SaveLoopJob` / shared deadline with `processDeferredSaveState` (later phase).
- DisplayCacheJob / export / undo-trim migration (Phase C).
- Changing Slot hydration lifecycle semantics or atomic Commit.
- New Workspace/Scratch/Context types for job temp state.

## Decisions

| Decision | Choice | Alternatives | Rationale |
|----------|--------|--------------|-----------|
| First ship | Thin `runFrame` → existing `StorageManager::runDeferredFrame` | Big-bang move of all LoadLoopJob fields | Proves main-loop wiring; zero behavior change |
| Job registry MVP | Single active + one parked load (as today) | Full multi-job heap queue | Match Phase A capacity before expanding |
| Priority | Reuse focus High / Low + `canRunBackgroundLoadLoopNow` | New priority enum in scheduler only | Keep proven gates |
| Commit | Still `StorageManager` / `applySnapshotToLoop` one-shot | Scheduler publishes Loop | Scheduler must not understand storage formats |
| Save | Stay on `processDeferredSaveState` | Force SaveLoopJob in B.1 | Avoid dual ownership risk in one session |

## Risks / Trade-offs

- [Dual entry points during migration] → Mitigate: only `main` calls scheduler; deprecate direct `runDeferredFrame` from other callers (`rg`).
- [Regression of 230145 smoothness] → Mitigate: B.1 behavior-preserving; device gate before B.2 queue rewrite.
- [Formal ownership trigger] → Mitigate: this OpenSpec + ARCHITECTURE-REVIEW gate before firmware; DEC-027 already accepted.

## Migration Plan

1. **B.1** — Add `DeferredJobScheduler::runFrame` delegating to `StorageManager::runDeferredFrame`; switch `main`.
2. **B.2** — Extract load step API (`stepSubmittedLoadJobs(deadlineUs)`) called only from scheduler.
3. **B.3** — Scheduler-owned active/parked selection; StorageManager submit/begin only.
4. **B.4** — Native tests (demote, frame budget); device gate vs `230145`.
5. Rollback: restore `main` → `StorageManager::runDeferredFrame`.

## Open Questions

None blocking B.1. SaveLoopJob admission timing deferred to a follow-on change.
