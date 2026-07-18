## Why

Phase A proved time-budgeted `LoadLoopJob` under `StorageManager::runDeferredFrame` (gates [`224607`](../../../captures/session_20260718_224607.log), [`230145`](../../../captures/session_20260718_230145.log)). DEC-027’s north star still requires a single **`DeferredJobScheduler`** so load, save, and later display/export jobs share one execution owner instead of each manager inventing a frame loop.

## What Changes

- Introduce **`DeferredJobScheduler`** as the main-loop owner of non-realtime deferred job execution (`runFrame(budgetUs)`).
- Migrate **execution** of `LoadLoopJob` from `StorageManager::runDeferredFrame` into the scheduler; `StorageManager` **submits** and owns job domain logic / Commit.
- Keep Phase A policies: demote-on-focus, atomic Commit, µs budgets, Low skipped while PLAYING, focus High while PLAYING.
- Phase B MVP: LoadLoopJob only in the scheduler queue. Deferred save stays on `processDeferredSaveState` until a later SaveLoopJob phase (not this change’s first ship).
- **BREAKING** (internal API): `main` calls `DeferredJobScheduler::runFrame` instead of `StorageManager::runDeferredFrame` once migration lands.

## Capabilities

### New Capabilities

- `deferred-job-scheduler`: scheduler owns frame execution, priority/demote, slice budgets; jobs remain resumable; managers submit.

### Modified Capabilities

- `lazy-slot-hydration`: deferred load execution path is via `DeferredJobScheduler` (behavior unchanged; owner name changes).

## Impact

- Code: new `DeferredJobScheduler` (+ job registry/queue), `main.cpp` frame hook, `StorageManager` submit/step API surface.
- Docs: [`deferred_job_scheduler_architecture.md`](../../../docs/plans/deferred_job_scheduler_architecture.md), Phase B enhancement plan.
- Tests: native demote/preemption/scheduler frame; device gate vs [`230145`](../../../captures/session_20260718_230145.log) smoothness.
- Does **not** change Commit immutability, MIDI/playback ownership, or product Workspace/Slot vocabulary.
