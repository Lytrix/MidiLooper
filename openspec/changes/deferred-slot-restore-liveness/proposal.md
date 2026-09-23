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

- **A queued slot restore becomes a liveness obligation.** Every queued restore SHALL reach exactly
  one terminal state — committed, unnecessary, or a named refusal — rather than remaining queued
  across transport activity, session changes and clears. Today `canRunBackgroundLoadLoopNow()`
  returns false while the transport runs or any track is recording or audibly playing, so a queued
  background restore simply never begins; a later transport start does not re-arm it.
- **The queue SHALL NOT head-of-line block.** `popNextDeferredLoopSlotRestore` takes the priority
  head, and the two `popNext` call sites cannot skip an entry whose slot is owned by a live owner;
  one blocked entry stalls every entry behind it.
- **A pending restore SHALL NOT block the deferred-save drain indefinitely.** The drain reports
  `BlockedBySlotLoad` for `SlotLoadSession::isActive() || loadLoopJobHasOpenSdFile()`; because the
  gated restore cannot advance in that state, CLEAR's blocking drain spins its whole stuck budget and
  the clear aborts. The gate and the liveness obligation SHALL be reconciled in one owner.
- **Restore and catalog state SHALL be observable enough to diagnose without a bisect:** the named
  terminal per restore request, the queue's blocked-vs-startable state, and the boot reconcile
  outcome (`BootRecovery` currently only prints `WARN: could not reconcile … index.bin`).
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
  be skipped while playing / SHALL defer until capture is idle" to a bounded liveness obligation,
  plus queue-progress, persistence-drain reconciliation, and restore-observability requirements.
  The boot-playback-first and hydration-lifecycle requirements keep their existing behavior.

## Impact

- `src/StorageManager/LoadLoopJob.cpp` — `canRunBackgroundLoadLoopNow`, `ensureActiveLoadLoopJobSelected`,
  `stepSubmittedLoadJobsImpl`, `deferredSaveBlockedByActiveSlotLoadSd`; the restore admission and its
  named terminal.
- `src/StorageManager/LoopSlotRestoreQueue.cpp` — the priority queue and its pop/peek helpers.
- `src/StorageManager/SyncDrainSaveState.cpp`, `src/StorageManager/DeferredSaveScheduler.cpp` — the
  drain gate and its failure classification.
- `src/StorageManager/BootRecovery.cpp`, `src/StorageManager/SavedSetIo.cpp` — the reconcile outcome.
- `src/main.cpp` — the boot drain-turn wiring around `bootInteractiveReady()`.
- Tests: `test/test_storage_manager_load` (the only suite that links the load path) and a new
  discriminating fixture set; `test/test_continuous_capture` only where a production seam is shared.
- No new module, no ownership move: `StorageManager` retains the load/restore owner and the deferred
  save scheduler retains persistence. Load-bearing constraint from `docs/Authority/ARCHITECTURE_RULES.md`:
  no allocation on the action edge, no SD write on the stop hot path, session-capture-gated
  instrumentation, and the `scripts/ram1_budget.json` post-link RAM1 floor must be respected.
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

## Open decisions (TBD — do not assume)

- **TBD-1 (device evidence).** Whether the observed stuck restore is dominated by the transport gate
  alone, or whether a second condition (an owned slot, a queue head that can never start, a stale
  `isLoopSlotRestoreAttempted` flag, or the boot reconcile leaving `pendingRestoreQueue` stale) also
  contributes. The discriminating fixtures below are designed to separate these; the fix is chosen on
  that evidence, not on the reading above.
- **TBD-2 (product).** Whether a background restore may consume a bounded slice of the MIDI service
  budget while a track is *recording*, or must wait for capture to end. `lazy-slot-hydration` says SD
  restore SHALL NOT run while capture is active; the correctness fix for the *playing* case does not
  require changing the recording case, and this change keeps recording deferred unless evidence shows
  the same wedge there.
- **TBD-3 (architect).** If bounding the drain's slot-load wait requires the drain to *cancel* or
  *supersede* an in-flight restore, that is a persistence-ownership change and needs its own `DEC`
  entry before implementation. The preferred path is to make the restore progress instead, so the
  drain never has to supersede it.

## Known behavior, evidence-backed assumptions, and UNKNOWNs

**Known behavior (read from code, cited).**

- `StorageManager::bootInteractiveReady()` (boot navigation) is
  `pendingLoopSlotRestoreCount() == 0 && !SlotLoadSession::isActive() && !anyLoadLoopJobActive()`.
- `SlotLoadSession` is constructed only by `beginLoadLoopJob` (one session per active job), so
  `SlotLoadSession::isActive()` is true exactly while a `LoadLoopJob` exists.
- `deferredSaveBlockedByActiveSlotLoadSd()` is `SlotLoadSession::isActive() || loadLoopJobHasOpenSdFile()`.
- `canRunBackgroundLoadLoopNow()` returns false when `bootTitleLoadDrain_` is false and either
  `sequencerRunning` is true or any track `isRecording()` / `isAudiblePlaybackActive()`.
- `ensureActiveLoadLoopJobSelected` skips a queued entry whose slot is not free
  (`queuedRestoreMayBegin`, the S2b.5 guard) and leaves it queued.
- `isLoopSlotRestoreAttempted` short-circuits both pop helpers, which is how an entry leaves the queue
  without a successful commit.

**Evidence-backed assumptions.**

- The RWSO-24CAP `DIAG,lcr,skip,restore` + `Track EMPTY` + census-changed-between-boots cluster is
  the same defect as the `Clear aborted` cluster, because both are observations of a non-empty restore
  queue with a blocked job. The 2026-09-23 session recorded them together and could not separate them
  from existing telemetry; separating them is a task, not an assumption this proposal relies on.
- The reconcile warning and the restore stall are at least partly independent: `reconcileSetIndexOnSd`
  is self-healing since the SD temp-write fix, yet the `Track EMPTY`/slope-sweep failures persisted
  after that fix landed.

**UNKNOWNs (to be resolved by task D0, before behavior edits).**

- Whether a device whose transport is started immediately at boot (as the canonical base stimulus
  does) can ever drain the boot background-fill queue. No log in hand shows the queue draining under a
  running transport.
- Whether the stuck `LoadLoopJob` is ever *parked* (the second job slot) in the failing runs, and
  whether a parked job is reachable by the drain gate's classification.
- The exact reason the `(track,slot)` census changed between boots — reconcile, partial restore, or a
  stale `loopSlotPayloadOnSdInRam_` entry.
- Whether `!CAPTURE_CONTENT` can be made to read a slot that the restore queue still considers
  pending, which would let the content PASS proceed independently of this fix.
