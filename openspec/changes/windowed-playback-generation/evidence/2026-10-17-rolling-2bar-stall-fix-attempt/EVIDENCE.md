# Fix attempt for the rolling 2-bar stall: the first patch was production-inert and was withdrawn (2026-10-17)

**Owner request:** fix the missing second-overdub and undo/redo steps in the rolling-playback HITL and rerun it.

**Outcome: not delivered this session. No firmware change is in the tree, nothing was flashed, and the rolling run was
not repeated.** The diagnosis advanced and the first candidate fix was **rejected by focused review as production-inert**
for the shape it targeted; the corrected target is named below with the evidence that points at it, but it is not yet
red-proved by a native reproducer, so no patch was left in place. The device is unchanged (pinned default `08f43e45…`
from the previous session's restore; no upload happened in this session).

## The chain the device evidence establishes

From the rolling run (`captures/i5-rolling-base-2bar-01`, `32f6501d…`, `STIM-BASE-2-2-01@v1`, track 8 / slot 4):

1. The 2-bar Record stop completed (`RECS,stop,3,357,0,1893,1536,0`, raw 1893 > 1536 ⇒ the take **wrapped**) and its
   deferred `CaptureStopJob` stayed at `PendingStopPhase::RecordPublished` (`#STOP_STATUS … pending=1,phase=16,dest=1`).
2. The recipe's next step — an overdub on the same slot — started anyway (`ST,Track,PLAYING,OVERDUBBING`, `ODUB,stage,…`).
3. The overdub's stop press arrived (`MI,U,144,8,39,98`) and was **refused**: `Track::stopOverdubbing()` reaches
   `beginDeferredOverdubStop(Playing, …)`, which answers "a predecessor still owns the sole frozen/deferred admission
   role" — the documented DEC-067 capacity denial, whose premise is *"a later stop request is admitted once the
   predecessor retires"*.
4. The predecessor could not retire: `stepDeferredRecordRetirement` returns `Pending` while
   `loop.pendingSnapshotSessionExitQueued() && !loop.publishedSnapshotIsCurrent()`, so the queued **session-exit root**
   is the only discharge — and it never published. The qualification row reads
   `DIAG,snapshot_unit,kind=step,ph=0,ld=0,p=1,q=0,a=0,if=1,…` (`a=0` = construction not allowed to start).
5. Run-end state: `state=OVERDUBBING, pending=1, phase=16`, `#HEAP_STATS,refused=track_work`, no `RING,overflow`;
   the harness aborted at 128 s, so the second overdub and undo/redo never ran.

## Candidate fix 1 (withdrawn): hoist the discharge case in `canServiceBackgroundSnapshotOwnerTurn`

`Track::canServiceBackgroundSnapshotOwnerTurn()` opened with `if (isRecording() || isOverdubbing()) return false;`, which
looks exactly like the deadlock. A native case was written for it
(`test_pending_record_retirement_is_discharged_while_an_overdub_is_live`, one-bar take): it was **RED** at the gate
assertion, GREEN after hoisting the RecordPublished-discharge term above the live-capture refusal, and **red-proved** by
restoring the refusal (kills exactly that case). Focused suite 6/6 and full native 3,305 / 7 skipped / 3,298 succeeded /
0 failed with it in place.

**Focused review (one pass, adversarial) rejected it as production-inert**, with two concrete findings:

- **Both production callers veto first.** `main.cpp`'s background dispatch is wrapped in
  `trackManager.backgroundSnapshotWorkAllowed()`, and `TrackManager` returns false for the first track that
  `isRecording() || isOverdubbing()` — before `canServiceBackgroundSnapshotOwnerTurn()` is consulted. The other caller
  short-circuits the same way. For the stalled track (`state=OVERDUBBING`) the new branch is unreachable.
- **The selected-track route never calls the function at all.** `main.cpp`'s realtime route
  (`selectedTrackIsRealtime` includes overdubbing and a pending deferred stop) already calls
  `processDeferredIdleMaintenance` directly — which is why the Snapshot owner turn *is* delivered on the device and the
  stall survives it. So the blocking gate is downstream of the owner turn, not at its admission.

The review also confirmed the *safety* question is benign (publishing that root mid-overdub cannot disturb the live
capture: `enqueueLiveOverdubPasses()` returns early while the session exit is queued, and publication only swaps the
published box), so the remaining question is which gate still refuses — not whether publishing is safe.

## Candidate fix 2 — **REFUTED by the device's own rows** (`DIAG,snap_gate`)

The qualification image already emits the row that answers this, so no new device run was needed. `DIAG,snap_gate`
carries `hp` = "a published root exists", `sep` = session exit queued, `od` = overdubbing, `cl`/`idle`/`elig` = the
publication gate's terms, and `st`/`cu` = eligible turns and in-flight construction units. From the stalled run
(`captures/i5-rolling-base-2bar-01`, device µs 40.1–40.6 s, i.e. across the overdub start):

```
#CAP,40189466,…,snap_gate,t=7,s=3,a=3,u=2,p=1,sep=1,fc=1,wj=0,hp=0,lr=1,vc=1,rec=0,play=0,od=1,step=1,cl=1,idle=1,elig=1,st=414,cu=68,rv=0
#CAP,40492820,…,snap_gate,…,p=1,sep=1,fc=1,wj=0,hp=0,…,od=1,…,st=464,cu=168,rv=0
#CAP,40518319,…,snap_gate,…,p=0,sep=0,fc=0,wj=0,hp=1,…,od=1,step=0,cl=1,idle=0,elig=0,st=468,cu=0,rv=1
```

**The construction ran and completed while the overdub was live** (`st` climbing 414→464, `cu` 68→168 with `hp=0`), and
the session-exit root then **published and became current** (`hp=1, rv=1`, `sep=0`). So
`allowSnapshotConstructionStart`'s `(!isOverdubbing || !havePublishedSnapshot)` term was never the blocker — the guard
`havePublishedSnapshot` was false for the whole construction, exactly as it should be.

**And the job still did not retire**: `#STOP_STATUS … pending=1,phase=16,dest=1` at 80.9 s, 82.3 s, 83.8 s — long after
`publishedSnapshotIsCurrent()` became true. Therefore the retirement gate that remains is the one **after** the
root-currency gate in `Track::stepDeferredRecordRetirement`: the DEC-136/DEC-140 hold (`hasBoundRecordReturnWindowForPendingJob()`
with a reader that is not root-backed), whose count advances only "while no adoption boundary can come". On a 2-bar loop
the lane can never serve (`win_window_ineligible`/`win_not_servable`, measured ×5/×2 in the same run), so the reader can
never become root-backed, while the running transport keeps producing boundaries — the hold's own premise
("a successor nothing can advance") is met, but its **counting condition** is not.

That is the target: the hold's expiry must also recognise "this slot has no servable lane route at all", not only
"the transport is stopped".

## Candidate fix 3 — written, and **withdrawn for lack of a RED** (same session)

Target, derived from the refutation above: the two waits that assume "a root-backed window can arrive" —
the DEC-136/DEC-140 hold's audible branch (`if (isAudiblePlaybackActive()) { … reset the count … return Pending; }`) and
the S7.5b.5 release edge's `if (frozenCaptureServed) return Pending;` — were gated on one named predicate,
`laneCanServeThisLoop` (`loop.shouldAvoidFullVisualRebuild(len) && len % (2 * TICKS_PER_BAR) == 0`, the lane's own
`win_window_ineligible` terms), so a slot the lane can never serve counts down and retires instead of waiting forever.

**It was withdrawn** because no shape I could build red-proves it. Three variants of
`test_rolling_record_retirement`'s own recipe were tried, each GREEN *with and without* the change (mutation control:

- audible short take, destination PLAYING, root published — measured `bound=1` (the bound return window is there) but
  **no owed successor and no served frozen Capture view**, so the hold's inner branch and the release edge are both
  skipped and the job retires on the ordinary path either way;
- publishing the root at the **loop's own** owner (`stepPendingSnapshotPublish(4, true, 0)`) so no Track-level reader
  turn runs and a prepared successor stays unadopted — still no owed successor at retirement time;
- forcing the 2-bar geometry: `stopRecording` re-derives the loop length from the take (a pre-set 1536 came back as 768
  both times), so the 2-bar shape is not reachable through that stop path at all.

**The missing ingredient is now named exactly:** the device state has an **owed successor** (`#STOP_PREFIX ready=2`, a
complete unpublished ready view) or a **serving frozen Capture view** at the moment the job reaches its release edge. The
harness shapes above reach neither. The reproducer therefore belongs in
`test_shared_capture_finalization`, whose helpers build the frozen-reader successor and the served frozen Capture view
(`serviceFirstRecordingBar`, the wrap fixtures, `test_matched_one_bar_stop_publishes_its_root_and_retires_after_reader_handoff`'s
"stop before the reader handoff" point) — driven up to *just before* adoption, with the root then published at the loop
owner, and the transport left audible.

## Candidate 3 taken to a device-preview gate: **the suite refutes it** (four DEC-136 cases)

With the gate applied (`laneCanServeThisLoop` on both waits), both images built (RAM1 `392,136 + padding 1,080` default /
`392,280 + 936` qualification, hexes `a2bd536a…` / `6c495ade…`) and the **full native suite failed 4 cases**, all in the
DEC-136 root-return family:

- `test_live_capture_frozen_ready_survives_root_publication_and_stop_releases_source` (RWO suite, `Expected TRUE Was FALSE`)
- `rollingRecordHeldMismatchStopWithDeferredOwners` — "the completed root-backed successor must take over the reader the
  job's release waits on (S7.5b.5)"
- `rollingRecordHeldStopHoldsOwedRootSuccessorAcrossTransportStop` — "the held mismatch Stop must reach its
  complete-but-unadopted root-return successor"
- `privateRootReturnDemandDoesNotOutliveItsJobIntoNextTake` — "the route's Stop job must have been released"

**That is decisive and it changes the diagnosis:** a short take *is* the DEC-136 shape. There the hold is not waiting for
something impossible — DEC-136's whole point is that a **private** root-return successor is prepared on exactly such a
loop and the job must hold until it is adopted. Gating the hold on "the lane cannot serve this loop" removes the hold
from the one family that needs it, so the predicate is **wrong**, not merely unproven. Nothing was flashed.

**Reframed defect (the next thing to test).** On the device a successor *did* exist and was never taken:
`#STOP_PREFIX … ready=2` (a complete, unpublished ready view) with the root current (`snap_gate hp=1, rv=1`) and the job
still `pending=1, phase=16`. So the question is no longer "is the wait impossible" but **which adoption edge failed to
take a ready root-return successor on a 2-bar loop** — the E2 boundary adoption, the lane's boundary adoption (which needs
a servable loop), or the successor's own qualification for a 16-bar-only route. The DEC-136 fixtures show the route
completing on their own shapes; the device shape differs in exactly one measured way: the loop is 2 bars, so the lane's
entry refuses.

## State left behind

- **No production line changed**: `src/Track/TrackStateFacade.cpp` was restored byte-identically from its pre-attempt
  copy (`cmp`), and the branch's dirty tree is what it was (56 files, 3,257 insertions / 349 deletions vs `HEAD`).
- **No knowingly-failing test left**: the candidate fixture case was removed; `test_rolling_record_retirement` is back to
  its **5/5** baseline.
- **No device action this session**: no build upload, no flash, no SD write. The device is on the pinned default
  `08f43e45…` from the previous session's restore.
- **No DEC, no limit, no checkoff**; 6.4 / 3.15f unchanged at 29/60.

## Next (ordered, each with its own evidence gate)

1. Make `test_rolling_record_retirement` publish the **wrap root** for a 2-bar take (the harness needs the Snapshot
   owner's turn during the recording, or a helper that publishes it the way the VisualCache wrap does), then assert the
   session-exit **starts and publishes** while the overdub is live — that is the RED for candidate 2.
2. Only then change `allowSnapshotConstructionStart` (or its call site's `isOverdubbing` term) so a **stopped Record
   job's** queued exit is not starved by a later live capture, and re-run the red-proof.
3. Full gates + both builds, then the device rerun of `STIM-BASE-2-2-01@v1` on the rolling qualification image, and the
   control if it disagrees. The recipe's second overdub and undo/redo steps are the acceptance criterion.
