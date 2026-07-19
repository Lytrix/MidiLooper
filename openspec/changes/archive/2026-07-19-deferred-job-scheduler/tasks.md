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

## Architecture gate (Phase B.3)
- Owner: `DeferredJobScheduler::runFrame` → `selectSubmittedLoadJobs` then `stepSubmittedLoadJobs`
- Invariant: published Loop never partially modified; selection + step only via scheduler frame
- Ownership change: YES (selection call site moves to scheduler; approved Phase B.3)
- Transition change: NO
- Behavior-preserving: YES (001237 focus-while-parked + Committing-before-activate-skip)
- Reuse: YES — `LoadLoopSelectionPolicy` + existing `ensureActiveLoadLoopJobSelected` / demote/park
- Scope: `DeferredJobScheduler.*`, `StorageManager.h/.cpp`, `LoadLoopSelectionPolicy.*`, tests/docs

## 4. B.3 — Scheduler-owned selection

- [x] 4.1 Move active/parked selection policy call sites behind scheduler (still StorageManager storage)
- [x] 4.2 Native tests: demote/parked idle + focus High while PLAYING (`test_load_loop_selection_policy`)

## 5. Gates

- [x] 5.1 Device gate B.1 PASS [`231510`](../../../captures/session_20260718_231510.log) vs [`230145`](../../../captures/session_20260718_230145.log) — BTN 9/9; zero `parse_us`/`frame_us`; clean stop
- [x] 5.1b Device gate B.3/B.4 PASS [`022107`](../../../captures/session_20260719_022107.log) — focus loads under PLAYING (`done 0/1`, `done 2/0`); zero `parse_us`/`frame_us`; transport stop + SAVE completed; DFRAME alive after stop
- [x] 5.2 Archive when B.1–B.3 (or agreed MVP) pass + device gate vs `230145` if required
