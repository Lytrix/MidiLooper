## Why

A slot with committed SD content can stay `EMPTY` for a whole power cycle, and the same stuck
state refuses CLEAR. Measured on device (2026-09-23, `runtime-working-set-ownership` RWSO-24CAP):
`Track 6 loaded state: EMPTY` (and track 7), `DIAG,lcr,skip,restore`, a `(track,slot)` census that
changed between boots, and twice `Persistence drain stuck after 33 iterations … Clear aborted:
could not complete deferred save first`. The device had to be worked around by stopping the global
transport and idling until the owed save drained.

That is a liveness defect, not a capacity one, and it is now the single blocker for the remaining
device gates of `runtime-working-set-ownership` (S1.1 per-owner census, S1.3 slope to the failure
point, S1.4 undo/root baseline, S1.5 device refusal point, S2b.3 post-restore baseline, S2b.5 device
gate) and for the content-verified PASS. `docs/Runtime/PROJECT_STATE.md` already names it: a
`StorageManager`/load owner defect that "should be its own change".

## What Changes

- **A queued slot restore becomes a liveness obligation with an explicit identity and outcome.** Every
  restore request SHALL reach exactly one terminal outcome — committed, unnecessary, a named refusal, or
  superseded — or wait in a **named** wait state, rather than remaining queued and unexplained across
  transport activity, session changes and clears. A request is identified by
  `(track, slot, requestGeneration, setEpoch)`, so a later session reusing the same coordinates is a
  different request, and recording deferral stays a **wait**, never a terminal refusal. Today
  `canRunBackgroundLoadLoopNow()` returns false while the transport runs or any track is recording or
  audibly playing, so a queued background restore simply never begins; a later transport start does not
  re-arm it.
- **The promise is scoped to what is bounded today, and it is conditional.** Preparation (open, read,
  parse) SHALL advance under active transport within its declared budget **while no restore job is
  commit-pending**. Two gates bound it: `runDeferredLoadAndDisplayFrame` gates commit and cleanup behind
  `MidiServiceDrain::admitLoadJobChild(timingCriticalTrackActive, activeCommitPending)` (whose comment
  says the publish "has no content-independent active-transport bound yet"), and
  `hasLoadLoopJobCommitPending()` is true when **either** the active or the parked instance is in
  commit/cleanup, so the denial is **global, not per job** — once one request reaches commit, no other
  request's preparation advances either. Both are named waits (`waiting_commit_gate`,
  `waiting_job_capacity`), never terminals and never unexplained stalls. Bounding them is the open clause
  of **`resumable-midi-boundary-playback-maintenance` task 3.3**, which this change names as an external
  prerequisite rather than absorbing; narrowing the gate per phase is a separate reviewed change (design O5).
  **Consequently this change delivers diagnosable preparation and named waits plus drain advancement where
  the gate admits — it does not by itself unblock CLEAR while another track keeps playing.**
- **The queue SHALL NOT head-of-line block.** `popNextDeferredLoopSlotRestore` takes the priority
  head, and the two `popNext` call sites cannot skip an entry whose slot is owned by a live owner;
  one blocked entry stalls every entry behind it. **This is a queue-ordering fix only** and does not
  address the scheduler-level `waiting_job_capacity` blockage above.
- **The blocking drain SHALL advance the restore it waits on, not merely observe it.**
  `drainPersistenceWorkBlocking` loops on `processDeferredSaveState` + `yield()` and never invokes the
  load scheduler, and a **parked** job keeps its `SlotLoadSession`, so the restore cannot progress and
  `deferredSaveBlockedByActiveSlotLoadSd()` cannot clear. The drain SHALL service the active **and**
  parked job instances through the existing selection/admission, deriving its admission from the **same
  all-tracks runtime eligibility** the normal path uses — **not** from an assumption that CLEAR leaves the
  runtime stopped, because `handleClearTrack` stops only the selected track. Where that eligibility denies,
  the drain preserves the request and reports the named wait. Restore work SHALL also be charged in the
  drain's step budget and progress detection, so a large restore cannot exhaust the budget or be
  misreported as stuck, and the drain SHALL NOT become an unbounded background fill.
- **Restore and catalog state SHALL be observable enough to diagnose without a bisect:** the request's
  state (terminal or named wait, with its reason) per restore request, the queue's blocked-vs-startable
  state, and the boot reconcile outcome (`BootRecovery` currently only prints
  `WARN: could not reconcile … index.bin`).
- **Acceptance fixtures SHALL assert the desired behavior, not the present failure.** A fixture that
  passes on the broken build is not a red test; old-state observations are kept as separate diagnostic
  assertions. The known-red acceptance set is excluded from the initial green-suite gate behind a named
  flag and promoted in the commit that makes it green.
- **The existing normative texts that permit this stall are amended, not bypassed:**
  `lazy-slot-hydration`'s "background Low `LoadLoopJob` work SHALL be skipped while any track is
  PLAYING" and its recording-defer scenario.

## Capabilities

### New Capabilities
- None. Every requirement below modifies `lazy-slot-hydration`, which already owns the boot
  hydration lifecycle, boot-playback-first commit, and deferred slot-load priorities. A new
  capability path would duplicate that owner.

### Modified Capabilities
- `lazy-slot-hydration`: the deferred-slot-restore admission rule changes from "background work MAY
  be skipped while playing / SHALL defer until capture is idle" to a bounded liveness obligation scoped
  to preparation, plus request-identity/state-separation, queue-progress, drain-**advancement**, and
  restore-observability requirements. The boot-playback-first and hydration-lifecycle requirements keep
  their existing behavior.

## Impact

- `src/StorageManager/LoadLoopJob.cpp` — `canRunBackgroundLoadLoopNow`, `ensureActiveLoadLoopJobSelected`,
  `stepSubmittedLoadJobsImpl`, `deferredSaveBlockedByActiveSlotLoadSd`; the restore admission and its
  state record.
- `src/StorageManager/LoopSlotRestoreQueue.cpp` — the priority queue and its pop/peek helpers.
- `src/StorageManager/SyncDrainSaveState.cpp` — the drain must gain a restore-service step for the
  active **and** parked job instances, derive its admission from the same all-tracks runtime eligibility
  as the normal path, and report a named wait reason instead of the generic `BlockedBySlotLoad` verdict.
- `include/PersistenceSyncDrainBudget.h`, `src/PersistenceSyncDrainBudget.cpp` — `SyncDrainBudgetInput`
  and `SyncDrainProgressSnapshot` / `madeSyncDrainProgress` gain restore terms, so restore work is charged
  in the drain's step budget and restore motion counts as progress.
- `src/StorageManager/DeferredSaveScheduler.cpp` — the drain gate and the mid-pass flush are unchanged;
  the restore service is a sibling step, not a change to the save gate.
- `src/StorageManager/BootRecovery.cpp`, `src/StorageManager/SavedSetIo.cpp` — the reconcile outcome.
- `src/main.cpp` — the boot drain-turn wiring around `bootInteractiveReady()`, and the
  `runDeferredLoadAndDisplayFrame` admission seam reused by the Packet A fixtures. **The commit stop-gate
  in that seam is not modified by this change.**
- Tests: `test/test_storage_manager_load` (the only suite that links the load path) plus the fixtures for
  the production admission seam and the real blocking drain; `test/test_continuous_capture` only where a
  production seam is shared.
- No new module, no ownership move: `StorageManager` retains the load/restore owner and the deferred
  save scheduler retains persistence. Load-bearing constraint from `docs/Authority/ARCHITECTURE_RULES.md`:
  no allocation on the action edge, no SD write on the stop hot path, session-capture-gated
  instrumentation, and the `scripts/ram1_budget.json` post-link RAM1 floor must be respected.
- **External prerequisite (named, not absorbed):** the bounded Commit clause of
  `openspec/changes/resumable-midi-boundary-playback-maintenance/tasks.md` task 3.3. Any behavior or
  acceptance criterion that requires a restore to apply under active transport is blocked on it. This
  change does not implement, relax, or duplicate that bound.
- Blocked by this change: the device gates of `runtime-working-set-ownership` and its content-verified
  PASS. Not blocked by it: `docs/Plans/sd_temp_write_truncation_refinement.md`'s reconcile-warning
  repair, which is landed and independent.
- Tracking: `docs/DELIVERABLE_TRACKING.md`; scope: `docs/Runtime/CURRENT_WORK.md`.

## Non-goals

- Resizing or re-architecting the slot-restore queue, the SD payload cache, or the hydration lifecycle.
- Changing what is persisted, the on-disk formats, or the save/revision flow. This is restore-side
  liveness only.
- Making restore synchronous, or restoring every slot before interactive ready (DEC-021 keeps boot
  deferred and boot-playback-first).
- Re-deriving the `runtime-working-set-ownership` residency law, replaying its S1/S2b device legs, or
  changing its accepted-stop/canonical-reservation contract.
- Changing CLEAR's user-visible behavior beyond removing the stall it currently hits. DEC-081 already
  ruled that audible playback never gates persistence.
- **Bounding Commit or cleanup under active transport.** That is the open clause of
  `resumable-midi-boundary-playback-maintenance` task 3.3 and is named as an external prerequisite, not
  absorbed here. Relaxing `MidiServiceDrain::admitLoadJobChild` in this change is explicitly out of
  scope.

## Open decisions (TBD — do not assume)

- **TBD-1 (device evidence).** Whether the observed stuck restore is dominated by the transport gate
  alone, or whether a second condition (an owned slot, a queue head that can never start, a stale
  `isLoopSlotRestoreAttempted` flag, or the boot reconcile leaving `pendingRestoreQueue` stale) also
  contributes. The discriminating fixtures below are designed to separate these; the fix is chosen on
  that evidence, not on the reading above.
- **TBD-2 (product).** Whether a background restore's **preparation** may consume a bounded slice of the
  MIDI service budget while a track is *recording*, or must wait for capture to end. `lazy-slot-hydration`
  says SD restore SHALL NOT run while capture is active; the correctness fix for the *playing* case does
  not require changing the recording case, and this change keeps recording deferred unless evidence shows
  the same wedge there.
- **TBD-3 (architect).** If bounding the drain's wait requires the drain to *cancel* or *supersede* an
  in-flight restore, that is a persistence-ownership change and needs its own `DEC` entry before
  implementation. The preferred path is for the drain to **service** the restore to completion (design D4),
  so it never has to supersede it.
- **TBD-4 (resolved by review — recorded here so it is not re-opened silently).** Whether this change
  promises that a restore **commits** while a track is timing-critical. **No.** The commit path is gated
  by `MidiServiceDrain::admitLoadJobChild(timingCriticalTrackActive, activeCommitPending)` and has no
  content-independent bound; bounding it is the open clause of
  `resumable-midi-boundary-playback-maintenance` task 3.3, which this change names as an external
  prerequisite rather than absorbing. The promise here is: never lost, never starved, never silently
  terminated, preparation advances under transport, and apply happens at the first admitted turn.

## Known behavior, evidence-backed assumptions, and UNKNOWNs

**Known behavior (read from code, cited).**

- `StorageManager::bootInteractiveReady()` (boot navigation) is
  `pendingLoopSlotRestoreCount() == 0 && !SlotLoadSession::isActive() && !anyLoadLoopJobActive()`.
- `SlotLoadSession` is constructed only by `beginLoadLoopJob` (one session per job instance), so
  `SlotLoadSession::isActive()` is true while **either** the active or the parked job holds one;
  `parkActiveLoadLoopJob` → `swapLoadLoopJobs` moves the `session` pointer, so a **parked** job keeps its
  session alive.
- `deferredSaveBlockedByActiveSlotLoadSd()` is `SlotLoadSession::isActive() || loadLoopJobHasOpenSdFile()`,
  and `loadLoopJobHasOpenSdFile()` checks **both** instances.
- `drainPersistenceWorkBlocking` loops on `StorageManager::processDeferredSaveState(state)` + `yield()`
  and never calls the load scheduler; `StorageManager::processDeferredLoopSlotRestore`'s only production
  caller is the main loop's deferred-load frame.
- `canRunBackgroundLoadLoopNow()` returns false when `bootTitleLoadDrain_` is false and either
  `sequencerRunning` is true or any track `isRecording()` / `isAudiblePlaybackActive()`.
- `runDeferredLoadAndDisplayFrame` denies the load-job child while
  `timingCriticalTrackActive && hasLoadLoopJobCommitPending()` (`MidiServiceDrain::admitLoadJobChild`),
  and its comment states the publish has no content-independent active-transport bound yet.
- `ensureActiveLoadLoopJobSelected` skips a queued entry whose slot is not free
  (`queuedRestoreMayBegin`, the S2b.5 guard) and leaves it queued.
- `isLoopSlotRestoreAttempted` short-circuits both pop helpers, which is how an entry leaves the queue
  without a successful commit.
- The boot scan offers **every** `(track, slot)` with a verified payload, and a request that is blocked
  later remains in `pendingLoopSlotRestores_`; there is no state in which a request exists but is neither
  queued, active/parked, nor terminal. (This is why the earlier "one-shot enqueue" mechanism C3 was
  withdrawn.)

**Evidence-backed assumptions.**

- The RWSO-24CAP `DIAG,lcr,skip,restore` + `Track EMPTY` + census-changed-between-boots cluster is
  the same defect as the `Clear aborted` cluster, because both are observations of a non-empty restore
  queue with a blocked job. The 2026-09-23 session recorded them together and could not separate them
  from existing telemetry; separating them is a task, not an assumption this proposal relies on.
- The reconcile warning and the restore stall are at least partly independent: `reconcileSetIndexOnSd`
  is self-healing since the SD temp-write fix, yet the `Track EMPTY`/slope-sweep failures persisted
  after that fix landed.

**UNKNOWNs (to be resolved by Packet A, before behavior edits).**

- Whether a device whose transport is started immediately at boot (as the canonical base stimulus
  does) can ever drain the boot background-fill queue. No log in hand shows the queue draining under a
  running transport.
- Whether the stuck `LoadLoopJob` is ever *parked* (the second job slot) in the failing runs, and
  whether a parked job is reachable by the drain gate's classification.
- The exact reason the `(track,slot)` census changed between boots — reconcile, partial restore, or a
  stale `loopSlotPayloadOnSdInRam_` entry.
- Whether `!CAPTURE_CONTENT` can be made to read a slot that the restore queue still considers
  pending, which would let the content PASS proceed independently of this fix.
