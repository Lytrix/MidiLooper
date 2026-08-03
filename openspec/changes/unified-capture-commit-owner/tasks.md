# Tasks — unified-capture-commit-owner

Plan: [`docs/plans/unified_capture_stop_driver_refinement.md`](../../../docs/plans/unified_capture_stop_driver_refinement.md).

**Review:** [ARCHITECTURE-REVIEW.md](./ARCHITECTURE-REVIEW.md) — per-phase architecture + implementation gates (restore archived OpenSpec review pattern).

## Phase 0 — Documentation and OpenSpec (no firmware)

- [x] Create `openspec/changes/unified-capture-commit-owner/` (proposal, design, tasks, specs)
- [x] Naming glossary in design + `capture-commit-owner` spec
- [x] Append DEC-023 to `docs/DECISION_LOG.md`
- [x] Update `docs/runtime/CURRENT_WORK.md` and `PROJECT_STATE.md`
- [x] [ARCHITECTURE-REVIEW.md](./ARCHITECTURE-REVIEW.md) with per-phase gates

## Phase 1 — Extract `commitCaptureForStop` (behavior-preserving)

- [x] Add `Track::commitCaptureForStop(CommitReason, commitTick, closeTick, …)` consolidating flush → `commitCapturePass` → `finalizeCommitSideEffects` from record sync path and overdub deferred stages
- [x] Record stop still synchronous; overdub FSM structure unchanged
- [x] **No** API renames; **no** scheduling changes
- [x] **Gate:** `pio test -e native`; existing HITL baseline unchanged
- [x] **Review:** [ARCHITECTURE-REVIEW.md](./ARCHITECTURE-REVIEW.md) § Phase 1 — architecture + implementation approval
- [x] **2026-07-19 restore (hygiene branch):** `commitCaptureForStop` + `prepareRecordStop` + `handleNoteEditFold` landed on `chore/codebase-hygiene-sprint1` — seal+finalize only (narrower than full Phase 1 flush ownership); see [`docs/plans/track_stop_dry_refinement.md`](../../../docs/plans/track_stop_dry_refinement.md)

## Phase 2 — Centralize runtime finalization (behavior-preserving)

- [x] Move post-finalize playback invalidation, single `requestDeferredSaveState`, display snapshot, shared `logCaptureCommitStage` into pipeline
- [x] Introduce shared post-commit helper; record and overdub both call it after `commitCaptureForStop` publishes
- [x] Remove duplicate save request from record stop tail; remove duplicate `resetPlaybackState` / `invalidatePlaybackWindow` from overdub FSM complete stage
- [x] **Structural gate (DEC-016 bridge):** one `playbackRevision` / invalidation path per publish — see refinement doc § Phase 2 acceptance
- [x] **Gate:** native tests; record and overdub HITL match pre-Phase-1 baseline
- [x] **Review:** [ARCHITECTURE-REVIEW.md](./ARCHITECTURE-REVIEW.md) § Phase 2

## Phase 3 — Deferred commit owner (first behavior change)

- [x] Introduce in-progress capture commit session on `Track` (reuse `overdubStopCommitStage_` fields initially)
- [x] Migrate record stop to queue + deferred FSM (same idle-slice policy as overdub)
- [x] Record preparation (quantize, truncation, state advance) in FSM before `commitCaptureForStop`
- [ ] **Gate:** acceptable record-stop latency; no capture loss; HITL arm → record → stop with display notes
- [x] **Review:** [ARCHITECTURE-REVIEW.md](./ARCHITECTURE-REVIEW.md) § Phase 3 — full PREFLIGHT if not done in PR

## Phase 4 — Lifecycle abort ownership

- [ ] Add `resetCaptureCommitSession()` and `cancelCaptureCommit()` (or rename from `cancelDeferredOverdubStop` in Phase 5)
- [ ] Replace scattered cancel call sites: `startRecording`, `clear`, `beginCapture`, `handleTransportStop`
- [ ] Guard in deferred processor for phase/reason mismatch
- [ ] **Gate:** native guards — freeze cleared on record start; no stale FSM after arm/clear/transport stop
- [ ] **Review:** [ARCHITECTURE-REVIEW.md](./ARCHITECTURE-REVIEW.md) § Phase 4

## Phase 5 — Rename cleanup (behavior-neutral)

- [ ] `OverdubStopCommitStage` → `CaptureCommitStage`
- [ ] `queueOverdubStopCommit` → `queueCaptureCommit`
- [ ] `processDeferredOverdubStop` → `processDeferredCaptureCommit`
- [ ] `cancelDeferredOverdubStop` → `cancelCaptureCommit`
- [ ] **Gate:** `pio test -e native`; canonical 64+64 HITL with `PERS,result,...,ok`
- [ ] **Review:** [ARCHITECTURE-REVIEW.md](./ARCHITECTURE-REVIEW.md) § Phase 5

## Closeout

- [ ] Update `docs/runtime/CURRENT_WORK.md`, `PROJECT_STATE.md`, `DELIVERABLE_TRACKING.md` when Phase 5 ships
- [ ] Archive change when green (`/opsx:archive`)
