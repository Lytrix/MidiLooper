# I5 — matched one-bar `RecordPublished` retirement packet

Status: **FOUND AND FIXED — DEC-140.** The device stall was reproduced natively and fixed inside the existing
bounded hold; native RED→GREEN and device-verified. The matched leg itself is still not accepted: its DEC-138
terminal now refuses on `stop_matching_era_not_advanced`.

## Identity and scope

- **Task / contract:** I5 — rolling Record diagnosis; `windowed-playback-generation` tasks 3.15b–c and 6.4/3.15f
  remain open. Reuse DEC-107 — bounded frozen-window retirement and DEC-136 — private committed-root preparation
  before Capture-ready adoption. DEC-138 — the non-truncating Stop terminal is unchanged.
- **Owner / path:** `Track::stopRecording` → real deferred Record finalization to
  `PendingStopPhase::RecordPublished` → Track's existing Snapshot deferred owner → `TrackPlaybackRuntime`'s
  current/ready/build reader roles and the clock-edge reader → `Track::stepDeferredRecordRetirement`.
- **Primary fixture:** `test/test_rolling_record_retirement/test_rolling_record_retirement.cpp` (new suite,
  registered in `scripts/native_test_link.py`'s firmware-boundary closure). It compiles
  `test_shared_capture_finalization`'s real Track/Loop/storage closure **with `PLAYBACK_WINDOW_ROLLING_ENABLED`**,
  the qualification image's shape; without it `Track::completeAdmittedRecordStop` cannot take the
  preserved-reader edge at all.
- **Path limits, stated:** `main.cpp` is not native-linkable, so the fixture drives the same `MidiServiceDrain`
  owner cursor/selector plus `Track::processDeferredIdleMaintenance`, the window lane's
  `serviceRollingPlaybackWindow` / offer / round-robin / `stepPlaybackViewMaintenance`, and
  `Track::playMidiEvents` as the clock-edge reader (`TrackManager::updateAllTracks` is outside this link closure).
  That is the seam the existing rolling Record fixtures use, not a copied state machine.
- **Scope:** one one-bar, non-truncating matched Stop on the selected Track/slot; Snapshot arbitration, reader
  handoff and retirement only.
- **Non-goals:** changing the Stop terminal or recipe acceptance, altering the service envelope, adding an
  owner/queue/state field, changing default rolling activation, reproducing physical timing/WCET, a device/content
  run, or any other slot/content operation.
- **Commit boundary:** one new native suite + the host link registration + this packet + DEC-139.
  **No production source line changed.**

## Oracle (fixed before behaviour edits)

1. Start the real rolling Record fixture on a prewarmed empty selected slot; accept one NoteOn/NoteOff pair inside
   the first bar and retain its exact channel, pitch, velocity, tick and NoteId.
2. Let the production `VisualCache` owner run `serviceFirstRecordingBar` while RECORDING, then stop through
   `Track::stopRecording` at a pre-wrap tick so the final geometry stays one bar. No injected `CaptureStopJob`, no
   copied state machine.
3. Drive the real deferred stop grain to `RecordPublished`. Assert the pass is committed once, the session-exit
   root is queued, the root is not current, the job owns its frozen range, the bound return window is held, and
   both the Track (`canServiceBackgroundSnapshotOwnerTurn`) and global (`backgroundSnapshotWorkAllowed`) Snapshot
   gates admit that stopped job.
4. **Control — withhold Snapshot turns.** Retirement must stay `Pending` with the same interval, phase and range
   owner, and must not release the bound source.
5. Grant Snapshot only through the production six-owner selector and `Track::processDeferredIdleMaintenance`.
   Assert the owner receives turns and the queued Record root becomes current; root currency alone must not
## Result and controls

Four shapes are asserted, and **all four pass** — the stalled `RecordPublished` obligation always ends inside a
bounded owner window:

1. `test_matched_one_bar_stop_publishes_its_root_and_retires_after_reader_handoff` — **Playing** destination. Ends
   on the root-backed reader (`Committed` + current `publishedSnapshotIdentity`), queue empty, bound return window
   cleared, sealed extent returned, content equal to the accepted pair.
2. `test_matched_one_bar_stop_to_stopped_publishes_its_root_and_retires` — **STOPPED** destination, i.e. the device
   probe's exact `state=STOPPED` / not-audible configuration. Also asserts the predicate fact
   `pendingSnapshotSessionExitQueued() ⇒ hasPendingSnapshotPublish()`, then that the queued root publishes and the
   job retires.
3. `test_stopped_recordpublished_job_retires_while_another_track_is_audible` — an audible sibling Track beside the
   stopped job; `backgroundSnapshotWorkAllowed()` and `canServiceBackgroundSnapshotOwnerTurn()` both stay true, the
   root publishes and the job retires while the sibling is still audible.
4. `test_stopped_recordpublished_job_retires_through_the_background_snapshot_unit_only` — the stalled Track is **not
   selected** (focus moved through the production `TrackManager::setSelectedTrack` route), so `main.cpp`'s
   selected-realtime branch cannot reach it and the bounded **background** unit is the only route. The fixture
   asserts the selected Track is served on its own turns, that the background unit is granted, that the non-selected
   job's root publishes, and that the job then retires.

**Gate-level hypotheses closed natively:**

- **`hasPendingSnapshotPublish()` vs `pendingSnapshotSessionExitQueued()` — RULED OUT.** The eligibility predicate
  cannot be true while the owner's own work gate is false: `pendingSnapshotSessionExitQueued()` returns
  `hasPendingSessionExitReceipt() || box.sessionExitQueued`, and `hasPendingSnapshotPublish()` returns `true` for
  both. Asserted directly in shape 2.
- **The timing reserve — RULED OUT for this shape.** `MidiServiceDrain::allowSnapshotIdleReserve` returns `true`
  unconditionally when `timingCritical` is false, and `timingCritical` is per-Track
  (`isRecording() || isAudiblePlaybackActive() || isStoppedRecording() || isArmed()`), false for the device's
  stopped, non-audible track.
- **Cross-track exclusion — RULED OUT.** Shape 3 keeps an audible sibling and the gates stay open.
- **Background-dispatch reachability — RULED OUT as a native defect.** Shape 4 puts the job behind the background
  unit only (not selected) and it still publishes and retires.
- **DEC-107 frozen-build stall — RULED OUT** earlier: `build_active=0` on every device poll means there was no
  build to stall.

**Red-proof (one mutation, byte-identical restore):** forcing
`Track::canServiceBackgroundSnapshotOwnerTurn()`'s `RecordPublished` term to `false` kills exactly shape 1 at its
eligibility assertion; `src/Track/TrackStateFacade.cpp` is restored to
`sha256 7a01e659b70a6ceaa2edd105453de81d4bb1759f28e21d6b5f2ba7ea43bd0010`.

**Rejected as fixes:** releasing on a stale root (the use-after-free S4f.19 prevents), extending the DEC-107
frozen-build stall bound, and re-running the device leg for a pass.

## Verification recipe and evidence

- `pio test -e native -f test_rolling_record_retirement` → **4/4 PASSED** (green with the mutation restored).
- `pio test -e native -f test_shared_capture_finalization` → **85/85** (unchanged; the shared suite carries no part
  of these cases).
- `pio test -e native` → **3 302 cases / 7 skipped / 3 295 succeeded / 0 failed**; the measured pre-change figure
  for this tree was 3 298 / 7 / 3 291, the delta being these four cases. The shared harness change (`capture_storage_harness.inc` honouring `HARNESS_OBSERVABLE_TRACK_SELECT`, default unchanged) costs nothing.
- `openspec validate windowed-playback-generation --strict` valid; `git diff --check` clean.
- No firmware build, RAM1 line or upload: no production source changed.
- Device/content verification: **NOT RUN and not authorized.** The matched leg stays failed; no full-recipe `PASS`
  may be inferred.
- Baseline context: branch `feature/unified-rolling-playback-path`, intentionally dirty worktree; unrelated changes
  preserved.

## The dispatch chain, read end-to-end (static trace)

Diagnostic #1 was also closed by reading the real chain, because the branch *choice* cannot be linked. Every gate on
the path is sound for a stopped, non-audible `RecordPublished` job:

- `main.cpp::loop()` → `MidiServiceDrain::allowDeferredIdleOwner(owner, wrapAppendPending)` only withholds
  `VisualCache` / `LoadAndDisplay` while a wrap-append is pending; **`Snapshot` is never withheld**.
- `runDeferredIdleMaintenance` returns early only for `SlotProvision`, `PlaybackViewMaintenance`, `Persistence` and
  `LoadAndDisplay`; the `Snapshot` owner falls through to the stopped/realtime split.
- All-stopped branch: the clock-running guard is satisfied because
  `allowStoppedClockSnapshotStep(true, true, anyPending)` reduces to `anyPendingSnapshotWork`, and
  `Track::hasPendingSnapshotPublishOnAnySlot()` is the union of the per-loop `hasPendingSnapshotPublish()` — which
  the queued session exit always satisfies. `MidiServiceDrain::selectStoppedSnapshotTrack` then returns the first
  **pending** track in cursor order, so a pending job cannot be passed over while any turn is granted.
- Realtime branch: the selected track is served when it is realtime (`hasPendingDeferredOverdubStop()` counts), and
  otherwise `backgroundSnapshotWorkAllowed()` + `canServiceBackgroundSnapshotOwnerTurn()` grant the background unit —
  both asserted true in shapes 3 and 4.
- Per-turn owner gate: `snapshotPending` (true), `snapshotIdleAllowed` (true for a non-recording/non-playing track via
  the session-exit clause), `snapshotEligible` (only refused for a *loaded-passes* publication, which a live Record
  session exit is not), and `allowSnapshotIdleReserve` (true when not timing-critical).

## Device leg (2026-10-17, authorized): the stall reproduces deterministically, and two causes are refuted

Track 8 / slots 4-5. Rolling qualification image **`2421c3359c4fe3e4c900a520ecd77d7c9559d2a2e67c3b594b680cba2acc3ce8`**
(no-build upload of the retained hex, hash verified before upload). Captures: `captures/i5-slot45-diagnosis-01/`.

- **Health baseline** (`health/health.json`): fresh attach, stopped, `pending=0`, queue `0/4`, slot 4 empty.
- **Reproduced three times** (`matched-02`, `matched-03`, plus the campaign): the matched pass ends
  `RuntimeError: record_evidence_drain_incomplete` and the run-end heap observation reads
  `#HEAP_STATS,refused=track_work` — the same signature as the original campaign failure. The first attempt after a
  flash refused in preparation (`scoped_record_preparation_unconfirmed`), the documented early-after-flash class.
- **The live stalled state** (`stalled-probe/stalled-probe.json`, sampled twice 40 s apart, byte-identical):
  `#STOP_STATUS state=STOPPED,pending=1,phase=16,queue=1/4`; `#STOP_PREFIX current=1,ready=0,build=2,retained=255,
  build_active=0,passes=1`; `#STOP_HISTORY storage=4,current=4,rows=0,prewarmed=1,pass=0`; and the qualification
  snapshot reads `currentRole=1 published=1 complete=1`, **`readyRole=0` with `readyFlags=1` (complete,
  unpublished)**, `buildFlags=0`, `workerFlags=0`, `requestActive=1`, **`requestInvalidated=1`**, `frameEpoch=0`
  vs `requestEpoch=3` vs clock `epoch=4`, `boundRole=255`, `framePeriod=0`, `stopResponsibility=1`, `rows=2`.
  `clockTick=3152` in **both** samples, so the transport clock was not advancing.
- **Refuted — multi-slot starvation (candidate #1).** Both authorized slots were cleared and verified empty, then
  the matched pass was re-run: it stalled again. A second populated slot on the same Track is therefore **not**
  required, and the `loopWithPendingSnapshotPublish` / `processSnapshotOwnerTurn` asymmetry is not the cause.
- **Refuted — the snapshot timing reserve.** `MidiServiceDrain::kSnapshotIdleReserveYieldEnabled` is `false`, so
  `processSnapshotOwnerTurn` skips the reserve branch entirely on this build.
- **Observed, recorded for its own sake:** clearing a **non-active** slot while the job is pending returns
  `ClearStatus.UNCONFIRMED` ("recognized long press with no clear/refusal evidence and no confirmed emptiness")
  because the Track is held by `track_work`, and that gesture left the Track `PLAYING` with the job still pending.
  Clearing the **job's own** slot is the route that works (as the earlier campaign showed).
- **Native attempt at the stalled state, and its limit.** A fifth case tried to build the same state (stopped,
  frame Idle, root current, complete-unpublished `Committed` ready successor) and **could not reach it**: with the
  transport stopped and no reader, the Track's window lane never produces a `Committed` ready successor
  (`the fixture needs a complete, unpublished root-backed successor in the ready role`). The case was removed rather
  than left red. **Conclusion:** on the device that successor was built while the transport was still running, so
  the reproducer must build it in the running phase and only then stop — that is the next fixture increment.

**Device restored:** default image `08f43e450ee327546b844af95a8b9ac57be46fcb8e84ad982ddda10e357fc964` no-build,
fresh boot banner `complete`, `pending=0`, queue `0/4`, STOPPED, overlay closed, heap complete; slot 5 empty, slot 4
holding the authorized scratch take. No SD cleanup, no other-slot content action.

## The defect and the fix (DEC-140)

The device's stalled state was reproduced natively and the responsible gate identified: retirement waited on a
complete, unpublished frozen-`Capture` ready successor being "adopted or cancelled by the Track window owner" — a
predicate that can never become false when the transport is stopped and no reader boundary can come. It also sat
*above* the DEC-136 hold, which is why that hold's counter read `hold=0` in the stalled state.

The fix deletes that un-bounded gate and makes the same successor a second `owedSuccessor` case inside the existing
bounded hold, so the accounting is shared rather than duplicated: audible ⇒ the boundary is the normal wait and the
count stays zero; no boundary can come ⇒ the count advances and, past `kRootReturnStallGrainLimit`, the successor is
torn through `discardCoverageBuild()` + `releaseFrozenRecordWindow()` so the job still releases. No new field, queue,
view or owner, and the RAM1 lines are byte-identical.

**Evidence.** Native: `test_unadopted_frozen_capture_successor_does_not_park_the_record_job` hung at
`steps=8192 pending=1 phase=16 root=1 exitQueued=0 hold=0 bound=1 readyComplete=1 readyPublished=0 readyOrigin=2(Capture)`
and is green after the fix (same tree, both directions). Device: qualification build **`32f6501d…`** (fix source),
matched pass re-run — `#STOP_STATUS` goes `pending=1, phase=16` → **`pending=0, phase=0, queue=0/4`**, run-end heap
observation **complete** (`v=1`), and the leg stops on the different, verification-level
`stop_handoff_unproven:['stop_matching_era_not_advanced']` instead of `record_evidence_drain_incomplete`.
Full native **3 303 / 7 skipped / 3 296 succeeded / 0 failed**; both builds SUCCESS against the unchanged 840 floor.

## Why the matched leg still refuses, measured (`matched-fix-01`)

With the stall fixed the leg reaches DEC-138's terminal and refuses `stop_matching_era_not_advanced`. The captured
pair shows the device behaving exactly as the preserved-reader design intends, and the *sample placement* being the
mismatch:

| term | before | after |
|---|---|---|
| tick | 264 | **776** (past the 768 wrap) |
| `epoch` / `frameEpoch` | 4 / 4 | 4 / 4 — **era unchanged** |
| frame | bound (role 0, storage 3, period 768) | bound (role 1, storage 4, period 768) |
| `(currentRole, currentStorage)` | (0, 3) | (1, 4) — rotated by the wrap |
| Stop processed | `recording=1`, `stopJob=0` | `recording=0`, `stopJob=1`, `captureToken` 2 → 4 |

The un-bounded gate this fix removed is exactly the fast path that *preserves* the reader
(`completeAdmittedRecordStop` returns before any frame reset), so the era does not advance and the serving view only
changes when the wrap adopts the frozen successor. DEC-138's era term and reader-identity term were measured on the
three-bar take, whose pre-wrap third window leaves ~150–350 ticks of margin; the one-bar matched take presses at
~640 on the grid (a host press lands ~160 ticks late), so its first post-stop *query* lands at 776, in the next
cycle. Both terms therefore fail by construction for this take and this observation cadence — not because the
firmware misbehaves.

**Fixture side fixed (2026-10-17).** The grid press moved from 36 to 46 clocks (nominal take tick ~400, measured
landing ~560) and the pre-stop sample no longer requires the narrow `256..440` phase window — a timing cliff the press
could step over (`matched-fix-02` failed `rolling_snapshot_not_recording` on it). `matched-fix-03` then samples the
post-stop state at **tick 688, inside the 768 cycle** (`raw 560 → final 768`, non-truncating) and the preserved-reader
fact is finally observable: `(currentRole, currentStorage)` `(0, 7) → (0, 7)` unchanged, with `recording 1→0`,
`stopJob 0→1`, `requestActive 1→0`.

**What still fails is the terminal, not the device.** The clock `epoch` is `8 → 8` in that run (`4 → 4` in
`matched-fix-01`): the clock era never changes across a Record stop in either measured take, so
`stop_matching_era_not_advanced` is unsatisfiable for this class — the same failure mode DEC-138 itself was created to
repair, one level deeper. The other matched terms also need re-specification from measurement (`after.captureToken ==
7` while `before.liveToken` had reached `9`).

**Disposition, not taken here:** replacing an acceptance term is a contract change and needs its own measured decision.
The candidate is a Stop-processed term the device actually produces — `recording` true→false **and** `stopJob`
false→true — keeping every other term and adding nothing weaker.

## What remains open

The matched leg is **not accepted**. Its terminal refuses on `stop_matching_era_not_advanced` — DEC-138's requirement
that the rolling frame's era advance across a non-truncating Stop. That is a recipe/oracle question about the terminal
itself, untouched by this fix, and it needs its own evidence: whether the device advances the era on this Stop path
(the frozen-reader fast path in `Track::completeAdmittedRecordStop` returns before any frame epoch bump) or whether
the terminal's era term is over-strict for this Stop class. No recipe `PASS`, 6.4/3.15f open at 29/60.
