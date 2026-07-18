# Architecture review — deferred-job-scheduler (Phase B)

**Change:** `deferred-job-scheduler`  
**North star:** [`docs/plans/deferred_job_scheduler_architecture.md`](../../../docs/plans/deferred_job_scheduler_architecture.md)  
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

- [ ] Architecture gate posted before firmware
- [ ] `pio test -e native`
- [ ] Device gate (B.1 optional if truly adapter-only; required before B.3 claim)
