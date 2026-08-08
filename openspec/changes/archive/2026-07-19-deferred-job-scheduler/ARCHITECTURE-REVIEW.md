# Architecture review — deferred-job-scheduler (Phase B)

**Change:** `deferred-job-scheduler`  
**North star:** [`docs/Plans/deferred_job_scheduler_architecture.md`](../../../docs/Plans/deferred_job_scheduler_architecture.md)  
**DEC:** [DEC-027](../../../docs/DECISION_LOG.md#dec-027-deferred-job-scheduler-north-star)

## North star

`DeferredJobScheduler` owns execution of non-realtime resumable jobs; domain managers submit and own Commit/domain logic.

## Per-phase gates

### Phase B.1 — Thin adapter

| Question | Required |
|----------|----------|
| **Owner module** | `DeferredJobScheduler::runFrame` (delegates to `StorageManager::runDeferredFrame`) |
| **Primary invariant** | Published Loop state never partially modified; load steps only inside scheduler frame |
| **Ownership change?** | YES — new scheduler entry (approved DEC-027 + this OpenSpec) |
| **State transition change?** | NO — LoadLoopJob phases unchanged |
| **Behavior-preserving?** | YES |
| **Reuse decision** | YES — extend existing `StorageManager::runDeferredFrame` behind adapter |
| **Phase scope** | `DeferredJobScheduler.*`, `main.cpp` call site, docs |

### Phase B.2+ — Step API / selection move

| Question | Required |
|----------|----------|
| **Owner module** | `DeferredJobScheduler` selects; `StorageManager` steps/Commits |
| **Primary invariant** | Same Commit atomicity; demote-on-focus preserved |
| **Ownership change?** | YES (deepens B.1) |
| **State transition change?** | NO unless selection policy changes |
| **Behavior-preserving?** | YES until SaveLoopJob lands |
| **Reuse decision** | YES — move call sites, do not fork LoadLoopJob |
| **Phase scope** | StorageManager load frame internals + scheduler |

## Implementation review checklist

- [x] Architecture gate posted before firmware (B.3)
- [x] `pio test -e native` (includes `test_load_loop_selection_policy`)
- [x] Device gate B.1 [`231510`](../../../captures/session_20260718_231510.log)
- [x] Device gate B.3/B.4 [`022107`](../../../captures/session_20260719_022107.log) vs [`230145`](../../../captures/session_20260718_230145.log)
- [x] Selection ownership: `DeferredJobScheduler::runFrame` → `selectSubmittedLoadJobs` → `stepSubmittedLoadJobs`
