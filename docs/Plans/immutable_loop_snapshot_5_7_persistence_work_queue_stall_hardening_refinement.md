# OpenSpec 5.7 — Persistence work-queue stall hardening

**Status:** **SHIPPED** (2026-09-12)  
**OpenSpec:** `immutable-loop-snapshot-passes` task **5.7**  
**Depends on:** [5.5 plan](immutable_loop_snapshot_5_5_deferred_persistence_ownership_refinement.md) (SHIPPED), [5.6 plan](immutable_loop_snapshot_5_6_large_loop_persist_bounds_refinement.md) (SHIPPED), [`current_set_persist_work_item_queue_enhancement.md`](current_set_persist_work_item_queue_enhancement.md) (B4 queue on `dev`)  
**Kind:** hardening (minimal firmware + host observability)

## North star (task 5.7)

Boot and post-clear workspace saves must **not wedge** when a `PersistenceWorkQueue` entry is left in **`Writing`** while `PersistenceWorkItemJob.itemActive` is false. A wedged queue stalls the entire deferred save (`pending` stays true, later `save_request` → `already_pending`, no `LoopPersist` / `PERS,result`).

5.5/5.6 verifiers assume save progress after record stop. A pre-record wedge is a **false `FIRMWARE_CONTRADICTION`** on an otherwise healthy core baseline.

## Evidence

| Capture | Result | Persist signal |
|---|---|---|
| [`000210`](../../captures/session_20260912_000210.log) | HITL core baseline **PASS**; formal **FAIL** (`persistence_stage_missing:dispatch`, no post-stop `LoopPersist`) | `SlotMeta,slot:4:0,start,ok` then **no** `bundle` / `bundle_done`; `persist_save` → **1 µs** no-ops for ~30 s |
| [`001343`](../../captures/session_20260912_001343.log) | HITL **PASS** (full persist verifiers) | `persistence_handoff result=ok`; `loop_persist=231 sliced=True` |
| [`231439`](../../captures/session_20260911_231439.log) | Prior 6.2.1b sign-off **PASS** | Normal boot `SlotMeta` → `LoopPersist` drain |

**Mechanism (hypothesis, log-consistent):** `beginPersistenceWorkItem` emits `PERS,work,...,start,ok` before bundle setup. If the job is reset without a successful `requeueWritingItem` (e.g. silent fail when `findEntryIndex` misses), `writingWorkItemCount() == 1` with `!itemActive` → `beginWriteQueuedItem` fails every `processDeferredSaveState` turn (~1 µs return).

**Not in scope:** 6.2 B3/B4 display compat; playback/display passed on `000210`.

## Architecture gate (task 5.7)

| Question | Answer |
|---|---|
| **Owner module** | `StorageManagerInternal::stepPersistenceWorkItem`, `PersistenceWorkQueue::requeueWritingItem`, `beginPersistenceWorkItem` |
| **Primary invariant** | No durable `Writing` queue entry without an active job advancing it; wedged state is detected and recovered in one bounded scheduler turn |
| **Ownership change?** | NO |
| **State transition change?** | NO — recovery only resumes existing admitted work |
| **Execution bound change?** | NO — O(1) queue scan / single requeue per detect |
| **Behavior-preserving?** | YES on happy path |
| **Reuse** | YES — extend existing queue + work-item owners; no new top-level Manager |
| **Phase scope** | Task 5.7 only |

### Architecture checkpoint

1. **Ownership?** NO  
2. **State transitions?** NO  
3. **Unbounded interval owner?** NO

## Stages

| Stage | Scope | Status |
|---|---|---|
| **5.7.1** | **Detect + recover** in `stepPersistenceWorkItem`: when `writingWorkItemCount() > 0 && !job.itemActive`, force-requeue the writing entry or reset to `Queued` with `#CAP` telemetry (`PERS,work_queue_stall` or `SC_PERSIST` diag) | **SHIPPED** |
| **5.7.2** | **Audit silent paths** in `requeueWritingItem`, `resetPersistenceWorkItemJobState`, defer-requeue after `start` telemetry — ensure `Writing` never survives without `itemActive` | **SHIPPED** |
| **5.7.3** | **Native fixture** simulating wedged `Writing`/inactive job; assert recovery unblocks `stepPersistenceWorkItem` | **SHIPPED** |
| **5.7.4** | **Host observability** (optional): `parse_persistence_buildup.py` or HITL warning when boot save has `start` without matching `bundle_done` before record stop | **SHIPPED** |

## Verification matrix

| Requirement | Gate | Status |
|---|---|---|
| Happy path unchanged | `pio test -e native` full suite | **1828/1833** |
| Wedged queue recovered | Native 5.7.3 fixture (`test_persistence_work_queue`) | **PASS** (13/13) |
| Boot save completes on HITL `base` | HITL post-fix run | **pending** device sign-off |
| Stall telemetry when recovery fires | `PERS,work,...,work_queue_stall,recover` on recover path | **SHIPPED** |
| Host stall warning | `parse_persistence_buildup.py` on [`000210`](../../captures/session_20260912_000210.log) | **PASS** |
| RAM1 floor | `teensy41-capture-serial` locals ≥ 4000 | **6880** |

## Explicit non-goals (5.7)

- Replacing the work queue or monolithic save scheduler
- Sync-drain policy changes (`SyncDrainSaveState`)
- Lowering 5.5/5.6 verifier thresholds for short loops
- Boot transport / multi-track playing-at-boot policy (document only if it correlates)

## Pre-implementation review

### Ready

- RCA anchored on `000210` vs `001343` / `231439`
- Owner path identified (`stepPersistenceWorkItem` + `PersistenceWorkQueue`)
- Architecture checkpoint all **no**

### Proceed?

**YES** — implement **5.7.1** first (detect + recover + telemetry), then native fixture **5.7.3**.
