# Task 3.10 (increment 27) — blocker 3, half 2: the deadline the admission promised, serviced on the live path

Baseline: `e65ceb48` (increment 26 landed). Branch `feature/windowed-playback-generation`.
Authority: DEC-108 §13(a) (*"the request is retained … the miss is reported"*), §13(c) (clock revalidation: the promise is
invalidated, never the content), §13(e) (the second miss is a named breach + latch); the spec's *A second miss latches a
fault, and continuity is preserved*.

## Why this slice exists, and what forced its shape

Increment 26 declared the envelope and compared the permitted gap — in the capture lane, as a review of that landing
correctly pointed out (see the `s310u` packet, Addendum 3). What remained is the half the qualification's blocker 3
names: **an operational overrun policy wired into the live admission/publication path.** Two facts were true at
`e65ceb48`:

1. The admitted promise (`PlaybackViewAdmittedWork::deadlineTick` / `clockEpoch`) was **written** by the admission and
   read by the budget and the request bookkeeping, and **no serving/publication path read it**.
2. The overrun policy that exists (`advancePlaybackLagTraversal` -> `resolvePlaybackLag` -> `one_pass_late` / `lag_breach`
   + `deadlineContractFaultLatched`) is driven by **revision currency** and has **no production caller**.

## The oracle: the review's six points, adopted verbatim

| # | Requirement (from the review) | How it is met |
|---|---|---|
| R1 | Check deadlines **independently of completion** — unfinished work can miss | `Track::serviceAdmittedDeadlineMisses(clock)` runs at the **top of every lane turn** for **every slot holding a live promise**; the fixture asserts the miss while `activeViewBuildWork()` is still non-null |
| R2 | Count each missed traversal **exactly once**, not once per turn | The traversals passed are computed from the clock (`1 + (tick - deadline) / loopLength`), the promise's own admission tick keys the count, and only the traversals that **change the policy** are accounted |
| R3 | Revalidate epoch/projection changes **without erasing obligations** | Judged only in the epoch the promise was qualified in; otherwise nothing is claimed and neither the promise nor the demand is cleared. A **new promise** resets the count and the episode to its own interval |
| R4 | Preserve the valid serving revision; **defer late adoption to an eligible boundary** | The service only accounts and reports: it publishes nothing, adopts nothing and rebuilds nothing (asserted: the published window is unchanged, the serving revision preserved) |
| R5 | **Enforce policy outside capture builds**; only reporting is capture-gated | The accounting and the latch are unconditional code; only the row goes through the capture seam. Red-proved by **M137** (gating the accounting behind `#if defined(SESSION_CAPTURE)` fails natively, where `SESSION_CAPTURE` is undef) |
| R6 | `replacementReady = false` alone is insufficient — the resolver can return `Current` from revision currency | The judgement is **never routed through** `resolvePlaybackLag`: the promise's own deadline decides, and only the *consequence* is shared. Fixture: a slot serving the **wanted revision** whose *next* promise ran late is still named late; red-proved by **M138** |

## What landed

* `PlaybackViewAdmittedWork::admittedIntervalLength` — the promise retains **the interval it is for** (`handoffPhase` is
  its start), so a late window is judged against its own traversal and the miss episode can be keyed on the interval the
  promise names.
* Per-slot `countedMissPromiseTick` + `countedMissedTraversals` — which promise the counts belong to and how many of its
  traversals have been counted: **once per traversal**, however many turns observe it.
* `Track::noteMissedTraversalForSlot(slot, loop)` — **the one implementation of what a missed traversal means**: the
  first miss of the episode is the allowance (`one_pass_late`), the **second consecutive** one is the named breach and
  sets the latch. `advancePlaybackLagTraversal` now *calls* it instead of duplicating the rule, so the revision rule and
  the deadline rule share one notion of a miss and differ only in what they judge.
* `Track::serviceAdmittedDeadlineMisses(clock)` — the per-turn service described above, unconditional.
* `Track::stepPlaybackViewMaintenance(..., PlaybackDeadlineClock clock = {})` — the turn takes the clock the lane
  dispatcher already holds (`src/main.cpp` passes `windowClock`); a clockless caller judges nothing, which is what keeps
  every existing caller (and every path without a promise) behaviourally unchanged.
* Size pins re-based **on measurement**: `sizeof(LoopPlaybackRuntime)` target 6,792 -> **6,816** B, host 8,744 -> **8,768**
  B (+24 B: the interval length, the 8-byte promise key and the 2-byte count). Per-slot extern storage: **no RAM1
  re-base**, `variables 96,192` unchanged, no DTCM.

## Fixtures (three, all in the ownership suite)

1. `test_an_admitted_deadline_miss_is_serviced_per_turn_once_per_traversal_and_never_re_granted` — R1, R2, R4: a turn
   inside the promise names nothing; a turn past it with the build **still in flight** names `one_pass_late`, leaves the
   deadline un-re-positioned, publishes nothing and keeps the obligation; five further turns at the same clock still
   count **one**; the **next traversal** names `lag_breach` and sets the latch.
2. `test_an_admitted_deadline_is_revalidated_across_an_epoch_change_and_a_new_promise_counts_from_zero` — R3: a foreign
   epoch claims nothing and erases nothing; the same promise in its own epoch is judged; a **new promise** on a sibling
   slot counts its own first miss (allowance, no latch) while the older promise stays at the traversal it had.
3. `test_a_late_arrival_is_named_even_when_the_served_window_carries_the_wanted_revision` — R6: the slot serves the
   wanted revision, a successor promise runs past its deadline, and the miss is **still** named; the serving revision is
   preserved.

## Mutations (red-proofs, byte-identical restores)

`src/Track/TrackPlaybackWindowBuild.cpp` pre-mutation hash **518728238943f77a**; every case restored to it.

* **M134** the per-turn service is never called -> *"an unfinished build that ran past its own deadline is named one pass
  late"* (`Expected 1 Was 0`), plus the epoch fixture.
* **M135** one miss **per turn** instead of per traversal (the traversal gate removed) -> *"Expected 1 Was 6"* and
  *"while the older promise stays at the traversal it had"*.
* **M136** the epoch guard dropped -> *"a promise is not judged in an epoch it was not qualified in"* (`Expected 0 Was 1`).
* **M137** the accounting gated behind `SESSION_CAPTURE` -> the same failures as M134, **natively** (which is R5's proof).
* **M138** revision currency allowed to mask a late arrival -> *"a promise that ran past its deadline is named late even
  though the served window already carries the wanted revision"* (`Expected 1 Was 0`).
* **M139** the deadline re-positioned on a miss -> *"and the deadline is never re-positioned"*
  (`Expected 1231680 Was 1462081`).

**One defect of mine, found by the fixtures and fixed:** the first accounting loop was bounded by the policy cap
rather than by the traversals that actually passed, so a *single* newly missed traversal called the helper **twice** and
the breach appeared at the first miss. It is now bounded by the number newly counted — and it is M135 that proves the
*traversal* gate, not the loop count, is what makes the counting per-traversal.

## Verification

* Focused `test_runtime_working_set_ownership`: **94/94** (+3 fixtures).
* `pio test -e native`: **2,737 cases / 2,730 succeeded / 7 skipped / 0 failed**.
* `pio run -e teensy41-capture-serial`: **SUCCESS**; RAM1 line byte-identical (`variables 96,192`, code 390,472 +
  padding 2,744); measured size pins only. Image `5fd778cadaa767a7…`, **not uploaded**.
* `git diff --check` clean; `openspec validate windowed-playback-generation --strict` valid.
* **Device: NOT RUN.** Production effect, stated rather than implied: no production path requests a window, so no promise
  is ever admitted, and the lane's production builds carry `clockEpoch == 0` — the service therefore judges nothing in a
  shipped build. What is now true is that the **policy is enforcement, not telemetry**: were a promise admitted, the miss
  would be counted, named and latched in a build without session capture.

## Blocker 3 — verdict

Both halves are now in: the permitted limit is **compared and named where the lane observes it** (capture telemetry), and
the **admitted deadline is serviced, named and latched on the live path, independent of capture** (enforcement). What is
**not** claimed: that the envelope is *qualified* as normal-operation continuity (the device legs showed blocked gaps
above the permitted advance, which is exactly why the overrun policy exists and reports), nor that activation is
complete — `sInterferenceBudgetEnforced` still awaits the calling owners' qualified clock stamps, and the production
requester, the re-attempt (O8) and the `H x K` sweep remain owed.
