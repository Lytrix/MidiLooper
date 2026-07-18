## 1. OpenSpec / docs

- [x] 1.1 Proposal, design, specs, tasks for `deferred-job-scheduler`
- [x] 1.2 Phase B plan in `docs/plans/deferred_job_scheduler_phase_b_enhancement.md`
- [x] 1.3 Update `CURRENT_WORK.md` / `PROJECT_STATE.md` to Phase B

## 2. B.1 — Thin scheduler adapter (behavior-preserving)

- [x] 2.1 Add `DeferredJobScheduler` with `runFrame(budgetUs)` delegating to `StorageManager::runDeferredFrame`
- [x] 2.2 Switch `main` / `runDeferredLoadAndDisplayFrame` to call scheduler
- [x] 2.3 Native compile + `pio test -e native`; `teensy41-capture-serial` build
- [x] 2.4 Architecture gate posted (ownership YES — approved Phase B.1 adapter)

## Architecture gate (Phase B.2)
- Owner: `DeferredJobScheduler::runFrame` → `StorageManager::stepSubmittedLoadJobs`
- Invariant: published Loop never partially modified; load steps only via scheduler frame
- Ownership change: YES (API rename; approved Phase B.2)
- Transition change: NO
- Behavior-preserving: YES
- Reuse: YES — rename `runDeferredFrame` → `stepSubmittedLoadJobs`; no fork
- Scope: `StorageManager.h/.cpp`, `DeferredJobScheduler.*`, docs/tasks

## 3. B.2 — Domain step API

- [x] 3.1 StorageManager exposes `stepSubmittedLoadJobs` used only by scheduler
- [x] 3.2 `rg runDeferredFrame` — no code callers outside docs/archive; `processDeferredLoopSlotRestore` routes via scheduler

## 4. B.3 — Scheduler-owned selection (optional same change)

- [ ] 4.1 Move active/parked selection policy call sites behind scheduler (still StorageManager storage)
- [ ] 4.2 Native tests: demote still parks; focus High while PLAYING

## 5. Gates

- [x] 5.1 Device gate B.1 PASS [`231510`](../../../captures/session_20260718_231510.log) vs [`230145`](../../../captures/session_20260718_230145.log) — BTN 9/9; zero `parse_us`/`frame_us`; clean stop
- [ ] 5.2 Archive when B.1–B.3 (or agreed MVP) pass
