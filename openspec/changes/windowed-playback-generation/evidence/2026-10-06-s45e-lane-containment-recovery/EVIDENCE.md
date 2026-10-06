# 4.5 — containment and recovery are driven by the lane's own serving boundary (firmware + tests)

Slice of task **4.5** (its **recovery half**) on task **3.7a**'s serving path. Packet:
[`EXECUTION.md`](EXECUTION.md). Commit message: *4.5 recovery — containment and recovery are driven by the lane's own
serving boundary*.

## 1. What was wrong, measured before the edits

`Track::advancePlaybackLagTraversal`, `Track::containmentForSlot` and `Track::applyLastResortForSlot` had **no production
caller**: `src/` contained only their definitions, and the only callers anywhere were tests. Two consequences, both
repaired here:

* the latched **deadline-contract fault never cleared in production** — the *only* clear site is
  `advancePlaybackLagTraversal`'s `Current` branch (`lag.missedTraversals = 0; lag.breachReported = false;
  runtime->deadlineContractFaultLatched = false;`), so a fault latched through the one route that *was* wired (an
  admitted promise whose deadline passes, `serviceAdmittedDeadlineMisses` → `noteMissedTraversalForSlot`) was permanent;
* the **approved last resort was unreachable**: nothing consulted `containmentForSlot`, so the exception DEC-108 §13(f)
  grants could never be applied.

The measured first failure of the new fixture (pre-slice) was exactly that: *"the eligible boundary clears the latched
fault: recovery through the real consumer, not a silent reset"* — the fault stayed latched across a real adoption.

## 2. What landed

Three calls in `Track::resolveRollingPlaybackWindow` (FLASH-placed boundary work, the lane's own per-traversal frame,
reached from the clock path by the call that already exists — so no new ITCM call site in the default image):

1. **At the frame's adoption** (the eligible boundary — `clock.tick == frame.deadlineTick && phase == frame.startPhase`
   and `adoptPreparedPlaybackWindow` succeeded):
   `advancePlaybackLagTraversal(slot, frame.startPhase, span, loop.playbackRevision, /*replacementReady=*/true)` →
   `Current` → **the episode and any latched fault end here.** This is the production caller the task owed, and it is
   still the *only* clear site, so recovery is still "the serving path recovered", never a silent reset.
2. **At the frame's expiry** (a `Serving` frame whose traversal passed unserved):
   `advancePlaybackLagTraversal(…, /*replacementReady=*/false)` → the first miss is the **allowance**
   (`one_pass_late`, named once), a second consecutive miss of the **same interval** is the named **breach** (the latch).
   Called only at the boundary event, never per turn: the count is per traversal.
3. **At the point where the lane's own window stops serving** (the three `Serving`-phase rejections: the bound view is
   no longer current, is no longer the immutable window it was, or no longer covers the phase): `applyLastResortForSlot`
   — `NotNeeded` unless the fault is latched **and** no valid coverage remains, and slot-scoped by construction.

No new state, no new row family, no scheduler, no second authority; `PlaybackLagState` and the containment policy are
unchanged. `serviceAdmittedDeadlineMisses`' own counting is untouched.

## 3. Verification

* **Focused** `test_runtime_working_set_ownership` **176/176** (baseline 175/175), and the pre-existing rolling fixtures
  are unchanged (the admissible shape still reports *zero* `one_pass_late` rows: an adoption is `Current`, not a miss).
* **Full native** `pio test -e native` **2,855 cases / 7 skipped / 2,848 succeeded / 0 failed** (baseline 2,854 / 7 /
  2,847 from the I9 record; no suite ERRORED or FAILED).
* **Default capture build** `pio run -e teensy41-capture-serial` **SUCCESS** with the RAM1 line **byte-identical**
  (`variables 96,192` / `ITCM 393,216 (code 392,248 + padding 968)` / `locals 34,880`): the slice lives behind
  `PLAYBACK_WINDOW_ROLLING_ENABLED`, which that image keeps at **0**, so the shipping image is unchanged by
  construction and by measurement.
* **Lane-enabled build** `pio run -e teensy41-capture-serial-window-qualification` is **over its own approved budget
  before and after this slice** (measured A/B by stashing the slice): baseline `code 393,240 + padding 32,744` → with the
  slice `code 393,384 + padding 32,600`, i.e. **+144 B of ITCM** in the lane-enabled image, in the same
  documented silent-boot-hang shape I5 recorded when that env could not be flashed. **Named limit:** this new behaviour
  is therefore **not device-observable** until that env is funded; nothing about it is claimed at the device level.
* `git diff --check` clean, strict `openspec validate` valid.
* **Red-proofs, each restored byte-identically and sha256-checked** (`src/Track/TrackPlaybackWindowBuild.cpp`
  `01120d7405ae347f…`):

| id | mutation | measured |
|---|---|---|
| **RP-A** | the adoption judgement removed | **RED** — *"the eligible boundary clears the latched fault: recovery through the real consumer, not a silent reset"* |
| **RP-B** | the expiry judgement removed | **RED** — *Expected 1 Was 0. and that traversal is named as the one-pass-late allowance, exactly once* |
| **RP-C** | the containment application removed (all three sites) | **RED** — *Expected 1 Was 0. exactly the affected slot's one open note is closed* |

## 4. The fixture, and exactly what it measures

`test_containment_and_recovery_are_driven_by_the_lane_boundary` builds its own shape (a 24-bar demoted loop in the
selected track's slot 0, its root published, prewarmed, playing, and a **served window** established through the window
lane's own `requestPlaybackWindow` → `stepPlaybackViewMaintenance` → served-window path), then opens the lane's demand
with `Track::serviceRollingPlaybackWindow` and drives it with the suite's `rollingTestTick` (selector + one worker turn
every 64 ticks) — so `resolveRollingPlaybackWindow` runs from the clock path, as in production:

* **(1)** two consecutive misses of the frame's own interval latch the fault; the lane then prepares, builds and
  **adopts**, and the fault is **cleared** — recovery through the real consumer — with the served window untouched and no
  silencing row;
* **(2)** the content revision advances while the requester is not serviced, so the frame's traversal expires with the
  interval one pass late: exactly **one** `one_pass_late` row, **no** `lag_breach`, no latch;
* **(2a)** the lane re-serves an interval (the expiry ended that episode), so the two checks below run with the resolver
  engaged and a `Serving` frame;
* **(3)** the control: with the fault latched and a readable, covering window, **zero** closures for the marked sounding,
  the entry alive, and **no** `last_resort_slot_silence` row;
* **(4)** the exception: with the fault latched and the lane's own window no longer servable, exactly **one** closure for
  that slot's marked sounding, that slot's ledger cleared, a **sibling slot's** sounding untouched, the
  `last_resort_slot_silence` row present, and the transport unchanged.

## 5. Recorded rather than smoothed over

* **The first fixture used the neighbouring S4b.7a harness and had to be replaced.** `assert_s4b_7_a_demoted_target_
  through_the_turn` passed inside the fixture, but the run reported a *trailing* failure at the harness's own switch
  commit (`Expected 2 Was 0`) attributed to this fixture, and the suite ended ERRORED (SIGHUP) in every arrangement
  tried — including with this slice stashed, and including two different positions in the run order. The fixture is now
  **self-contained** (it neither borrows nor perturbs another fixture's preconditions) and the error is gone; the
  incident is recorded because it is a real trap for anyone else reusing that harness from a new fixture.
* **Two pre-existing suite entries were lost and restored.** A cut that was meant to remove my own duplicate
  `RUN_TEST` line took `test_window_contention_sweep_measures_the_composed_cost_across_h_and_k` and
  `test_window_contention_sweep_scales_with_density_and_is_span_independent` with it; a diff of the `RUN_TEST(...)` sets
  against `HEAD` caught it and both are back. The suite's entry list now differs from `HEAD` by exactly one line.
* **The control (3) is a control, not a discriminator.** It passes with and without the containment application by
  construction (nothing is applied while coverage remains); its job is to make the exception's *guard* assertable, and
  `RP-C`'s discriminator is (4).
* **The fixture sets two states directly, and names them:** the content revision it advances (the edit path that
  advances a revision is not this slice's subject) and the "window no longer servable" flag (`published = false`; which
  lifecycle event retires a published window belongs to that owner). Both are inputs, not claims.
* **The miss route's own cadence.** A miss is counted only at a boundary event and the interval identity passed is the
  frame's own (`frame.startPhase`, span), so the allowance is per interval and per pass — the rule
  `PlaybackLagState` already documents. With a sliding span, two adjacent frames are different intervals and therefore
  different episodes; the loop-pass route through `serviceAdmittedDeadlineMisses` keeps its own count.

## 6. Named, not claimed

* **No device leg.** The lane-enabled image cannot be flashed at all (its own guard fails; §3) and the default image does
  not contain this code, so nothing is claimed from hardware. No timing claim, no `ExecutionBounds.md` row change, no
  fallback removal (3.7b).
* **The exception is applied only while the lane is serving this slot.** The containment call sits in the resolver's
  `Serving`-phase rejections, so a slot the lane is not serving (an `Idle`/rejected/`Blocked` frame) is not silenced by
  it — the retained merged fallback keeps serving exactly as before. Whether "no valid coverage" should also cover
  *non-lane* serving is an owner-level contract this slice does not invent.
* **`serviceAdmittedDeadlineMisses` is unchanged**, so a promise miss that no lane frame is serving still latches
  without a boundary of its own.
* **4.5 stays open** on its remaining item: **displayed provenance** (the display's, per
  [`DISPLAY-CONSUMER-RECONCILIATION.md`](../../DISPLAY-CONSUMER-RECONCILIATION.md) → RWO **S4e**). The recovery gate
  itself is delivered here; **3.7a stays open** (3.7b's conditions are unchanged), and 3.14 is untouched.
