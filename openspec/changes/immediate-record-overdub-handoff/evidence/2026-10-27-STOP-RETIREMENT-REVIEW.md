# 2026-10-27 — STOP-RETIREMENT-REVIEW (post-DEC-165 code review + this slice's execution packet)

Scope: review of the DEC-165 landed deferred-stop escape machinery, and the
owner-approved repair slices for the wedge that DEC-165's cause-based escape did
not actually close. Branch `feature/unified-rolling-playback-path` (verified from
Git; the AGENTS.md baseline name is stale), working tree
shared with the in-flight change (no commit, no upload).

## Finding 1 — BLOCKER: the cause-based escape retires nothing

`src/Track/TrackOverdubLifecycle.cpp` `CanonicalTail` escape (currently 3041-3056)
calls `(void)stop.retireSkippedCompanionPreparation(false)`, but
`PendingStopAdmission::retireSkippedCompanionPreparation`
(`src/Track/TrackPlaybackRuntime.cpp:270-279`) refuses whenever
`destination != Stopped` (or `emptyCapture`). Every job that reaches this route is
destination-`Playing` by construction:

- STOPPED-admitted jobs skip the companion tail at `PrepareTransfer` (3026-3031),
  they never reach `CanonicalTail`.
- Retargeted (Playing→Stopped) jobs are excluded by
  `stopCompanionPathIsUnreachable`'s `!retargetedFromPlaying` guard; they take the
  counted RawPassOnly route instead.

So the discarded call returns `false` 100% of the time on the only route that
reaches it. `remainingRankedPreparationGrains`, `remainingCanonicalPreparationGrains`
and `remainingUndoCollectionGrains` stay non-zero, `finalizationComplete()` stays
false, and `stop.canCommit` refuses both transfer variants
(`src/Track/TrackPreparedStopCommit.cpp:117/142`). The job ping-pongs
AtomicTransfer → `requiresBridgeRefresh` → CompanionBridge → (base still abandoned)
→ escape → AtomicTransfer forever, holding the `CaptureStopJobs::kCapacity = 8`
FIFO head: every later press is admitted behind it and retained
(`custody_cap_denied` / `successor_stop_denied_capacity`). This is the same
user-visible wedge DEC-165 named, surviving its repair.

Consequence (device): one destination-Playing stop whose canonical base was
abandoned (the DEC-165 AbandonBase route itself) re-wedges the whole stop ring at
`CanonicalTail` instead of `CanonicalPrefix`, unreachable from a capacity budget because
DEC-165's cause-based route has no budget.

## Finding 2 — duplicate guard

`src/Track/TrackOverdubLifecycle.cpp:3039/3040` read the identical
`if (stop.phase == PendingStopPhase::CanonicalTail) {` twice (an editing leftover
from the DEC-165 insertion; the inner body kept its extra indent, 3057-3064).
Removing the inner opener and inner closer is behavior-neutral.

## Finding 3 — silent per-job refusals on the ping-pong route

Named per DEC-166's "the lane that wedged reports none" standard, the route the
wedged job actually cycles has no per-job naming: `CompanionBridge`'s
`refreshFrozenCompanionBridge` / `beginFrozenCompanionBridge` /
`stepReservedFrozenCaptureBridge` refusals (3157-3189) and the `AtomicTransfer`
`retireSupersededPreparation` refusal (3220) all `return false` silent. With
Finding 1 fixed these refusals should never repeat, but the whole point of DEC-166
was that the *next* wedge must be attributable from serial capture alone.

## Repair design (this slice)

1. **Named estimate retirement** —
   `PendingStopAdmission::retireCompanionSkippedForBaseAbandonment()`: zero the
   order/ranked/canonical/undoCollection estimates with no destination gate; keep
   the `pending && phase == PrepareTransfer && remainingClosureGrains == 0`
   guards. The cause is destination-independent: the companion path can never
   *begin* on this job. The CanonicalTail escape calls it as a named refusal
   (`if (!stop.retireCompanionSkippedForBaseAbandonment()) return false;`) — it
   cannot fail there (phase set right before, closure complete), but the contract
   stays named. The other three `retireSkippedCompanionPreparation` call sites
   are byte-identical (their destination semantics are correct).
2. **No-window commit without a revision move** — at `AtomicTransfer`, extend the
   `!causalBaseCurrent` superseded branch with the abandoned cause
   (`!stop.retargetedFromPlaying && retargetStallPhase_ == 0xFF`), routing the job
   to `commitSupersededCausalBase` (`TrackPreparedStopCommit.cpp:334-356`: no
   bridge check, no companion check). Rationale: `commitPreparedStop` requires
   `canSelectReservedFrozenCaptureBridge`, which an abandoned base denies
   forever, so the plain Playing commit can never run for this job; the
   superseded route is the only terminal that commits the staged pass. This
   matches the "refuses an unnamed entry forever / fail-open" standard: pass
   commits, undo publishes only when an entry was prepared and publishable.
3. **Per-job refusal naming** — distinct `noteStallOnce` reasons at the
   CompanionBridge refresh/begin/step bail-outs and the atomic
   `retireSupersededPreparation` refusal. Pinned via the deferredStopStallNoted_
   latch (no reason-string seam exists in this suite; naming change red-proved by
   mutation).
4. **Duplicate guard removal** (Finding 2).

## Execution packet (G2)

- Scope: the four items above only; no estimate/bound/decision-table changes;
  the other three `retireSkippedCompanionPreparation` call sites stay
  byte-identical; no revision outside the abandoned-base cause.
- Transition oracle: RED fixture
  `test_abandoned_base_retires_the_playing_stop_without_revision_movement` in
  `test/test_shared_capture_finalization` — destination-Playing overdub stop with
  a live successor; sentinel manufactured via friend access
  (`retargetStallPhase_ = 0xFF`) because driving a genuine 2048-grain stall
  couples the fixture to content volume; assert (a) the job retires with the
  pass rows preserved, (b) `playbackRevision` never moves (no successor wrap —
  head retirement does not need one), (c) a successor stop on the other slot is
  then admitted (not retained) and drives to 0.
- Mutations: remove the named retire call → RED; remove the abandoned-cause route
  condition → RED; both restored byte-identically.
- Gates: focused `test_shared_capture_finalization` + `test_playback_prewarm`,
  then `pio test -e native`, then `teensy41-capture-serial` firmware build (RAM1
  unchanged expected: no new Track members).
- Closeout: DEC-167, tasks.md, PROJECT_STATE, CURRENT_WORK. No commit, no flash.

## Results (same file, appended 2026-10-27 after execution)

### RED → GREEN (fixture `test_abandoned_base_retires_the_playing_stop_without_revision_movement`)
- RED (pre-fix, suite build): fails at the retire assertion — `Expected 0 Was 1.
  abandoned-base Playing stop must retire, not wedge the head` — after a
  budget+4,096-turn drive; no later assertion runs. Exactly the Finding 1
  behavior (head retained, FIFO blocked).
- GREEN (post-fix), pinned, all asserted:
  - head retires (`jobs == 0`);
  - `playbackRevision` moves **exactly +1**: the transport never advances and the
    successor keeps capturing (also pinned: it is still recording afterwards),
    so no wrap can run — the only revision movement is the head's own commit
    publication. Oracle refinement during implementation: a strict
    "revision never moves" assert is wrong because every commit route bumps
    `playbackRevision` at publication (`LoopCapture.cpp` commit path); the
    honest pin is "exactly one publication, supplied by nobody else".
  - 140 note pairs commit intact (`overdubPasses.size() == 1`) — the rows are
    kept, DEC-165's trade stands;
  - the undo entry publishes (receipt-prepared at the atomic handler,
    `supersededPublishable` true): `undoCount == 1` after the head, `== 2`
    after the later successor record stop — better than fail-open, matches the
    DEC-163 receipt shape;
  - successor service: the next press (`stopRecordingToStopped` on the live
    successor) is **admitted immediately** (`jobs == 1`, no
    `hasAcceptedUnexecutedInteraction` retention) and `driveStopToRetirement`
    drains to `jobs == 0`; both slots request persistence. This is the
    "more than 8 overdubs can be committed" acceptance — the ring can no longer
    be wedged by one destination-Playing abandoned-base stop.

### Mutations (as-executed)
- **M1 (escape retirement removed)**: behavior-neutral at this shape. The
  atomic handler's own superseded branch calls `retireSupersededPreparation()`,
  which zeroes the same fields one turn later (its `remainingOrderGrains == 0`
  precondition always holds there because PrepareTransfer entered through
  `retireCompletedPreparation`). Recorded honestly: the escape-level named
  retirement is the contract DEC-167 §1 asks for (destination-independent,
  self-contained escape), not a behavioral lever at this fixture.
- **M2 (route-by-cause removed)**: RED reproduces pre-fix exactly —
  `Expected 0 Was 1` at the same assertion; suite runtime goes 8 s → 5:20 (the
  20k-turn ping-pong drive). Restored byte-identically (sha256
  `ff28ac82…d3f0d2` before/after match) and GREEN reconfirmed.

### Naming (Finding 3)
Distinct once-per-job `noteStallOnce` reasons landed at:
`companion bridge begin refused for this job`, `companion bridge step denied
for this job`, both `superseded causal base could not retire its estimate`
sites (`CompanionBridge` stale-base branch and `AtomicTransfer`), and
`atomic transfer commit refused for this job` (the pre-fix wedge point).
No reason-string seam exists in this suite (same standard as the record lane's
reporter, which is pinned on the stored term rather than log text), so naming is
verified by code review + retained-by-construction; no mutation-targetable
observable exists without adding state (out of scope, `sizeof(Track)` pin).

### Gates
- `test_shared_capture_finalization` **112/112** (new fixture included)
- `test_playback_prewarm` **252/252** (DEC-165 decision-table asserts intact)
- full native `pio test -e native` **3,524 / 7 skipped / 3,517 succeeded /
  0 failed**
- `teensy41-capture-serial` **SUCCESS**: RAM1 `variables 96,192` (pin unchanged,
  no new members), ITCM `code 390,472 + padding 2,744` (approved floor 840,
  1,904 B headroom, no re-base), FLASH free 6,790,148
- firmware hex built; **not uploaded, no hash proof of programming** (no upload
  was requested)
- no commit (working tree shared with ~1,770 unrelated in-flight display/playback
  lines)

### Device verification (owner upload, `captures/session_20261027_172742.log`)
The stop-ring wedge is **fixed on device**: 9 stop presses, all 9 interval jobs
drained (9 `ODUB,stop,atomic_transfer … causal_base_superseded` rows, 86.1 s →
121.9 s), both new escape rows fired (`canonical base stalled; stop continues
without it` at 86.1 s / 95.7 s), zero `stop_job_pending` retentions. This run
also surfaced a **new, separate defect** — starts refused after the 8th overdub —
attributed in
[`2026-10-27-QUEUED-RETARGET-EDGE-ATTRIBUTION.md`](2026-10-27-QUEUED-RETARGET-EDGE-ATTRIBUTION.md);
it is not a regression of this repair (the ring itself never wedged).

### Honest notes
- The fixture's first GREEN run caught an oracle defect (strict immutability
  assert) after the fix already landed, fixed to the exact +1 pin above; the RED
  run predates the fix and failed at the retirement assert, so fixture-first
  ordering holds for the behavioral change.
- The wedged-phase naming covers the ping-pong phases observed pre-fix; a run
  whose residual wedge lives elsewhere will still be named once per job by the
  pre-existing escapes (`canonical base unavailable; companion path skipped`
  etc.), whose latch consumption order is unchanged.

