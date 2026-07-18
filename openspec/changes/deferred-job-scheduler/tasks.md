## 1. OpenSpec / docs

- [x] 1.1 Proposal, design, specs, tasks for `deferred-job-scheduler`
- [x] 1.2 Phase B plan in `docs/plans/deferred_job_scheduler_phase_b_enhancement.md`
- [x] 1.3 Update `CURRENT_WORK.md` / `PROJECT_STATE.md` to Phase B

## 2. B.1 — Thin scheduler adapter (behavior-preserving)

- [x] 2.1 Add `DeferredJobScheduler` with `runFrame(budgetUs)` delegating to `StorageManager::runDeferredFrame`
- [x] 2.2 Switch `main` / `runDeferredLoadAndDisplayFrame` to call scheduler
- [x] 2.3 Native compile + `pio test -e native`; `teensy41-capture-serial` build
- [x] 2.4 Architecture gate posted (ownership YES — approved Phase B.1 adapter)

## 3. B.2 — Domain step API

- [ ] 3.1 StorageManager exposes load-step entry used only by scheduler (rename/wrap as needed)
- [ ] 3.2 `rg runDeferredFrame` — no stray main-loop callers outside scheduler path

## 4. B.3 — Scheduler-owned selection (optional same change)

- [ ] 4.1 Move active/parked selection policy call sites behind scheduler (still StorageManager storage)
- [ ] 4.2 Native tests: demote still parks; focus High while PLAYING

## 5. Gates

- [ ] 5.1 Device gate vs [`230145`](../../../captures/session_20260718_230145.log) — buttons 1:1; no ~295ms `parse_us` clusters
- [ ] 5.2 Archive when B.1–B.3 (or agreed MVP) pass
