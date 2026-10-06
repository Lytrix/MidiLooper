# G2 execution packet — 4.5: containment and recovery driven from the lane's own serving boundary

Slice of task **4.5** (its **recovery half**) on task **3.7a**'s serving path. Written before any edit, per the
[G2 execution packet](../../../../Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice)
requirement.

## 1. Why this slice, and why now

4.5's own OWED list still names *"production containment/recovery … eventual recovery through the real consumer"*, and
`RECONCILIATION.md`'s row *4.5 containment, last resort, closures* states the gap in the same words: *"positioned
promises now have a gated rolling requester, but containment/recovery is not integrated into that serving slice"*. Task
3.7a's text requires 4.5 to be finished *"against the real handoff, including their production-path interleavings and
**closure/recovery gates**"*; the closure half landed as I9, so this is the remaining half.

**Measured, not assumed (grep, before the packet):** `Track::advancePlaybackLagTraversal`, `Track::containmentForSlot`
and `Track::applyLastResortForSlot` have **no production caller at all** (`src/` contains only their definitions; the
only callers are tests). Two consequences follow, and they are the defects this slice repairs:

* the latched **deadline-contract fault never clears in production** — the *only* clear site is
  `advancePlaybackLagTraversal`'s `Current` branch (`lag.missedTraversals = 0; lag.breachReported = false;
  runtime->deadlineContractFaultLatched = false;`), so a fault latched through the one route that *is* wired (an
  admitted promise whose deadline passes, `serviceAdmittedDeadlineMisses` → `noteMissedTraversalForSlot`) is permanent;
* the **approved last resort is unreachable**: nothing consults `containmentForSlot`, so the exception DEC-108 §13(f)
  grants (targeted note closure plus affected-slot silence when no valid coverage remains) can never be applied.

## 2. Where the boundary is, and why it is there

DEC-108's owner for this is the lane's own serving path (item 1: *"one window-build/publication owner:
`Track::stepPlaybackViewMaintenance`"*, and item 13(a): *"**Pass = one traversal of the affected interval in the playback
projection**"*). The lane already keeps exactly that per-traversal frame: `LoopPlaybackRuntime::RollingFrame`
(`startPhase`, `loopLength`, `deadlineTick`, `phase`), and its serving resolver
`Track::resolveRollingPlaybackWindow` has **two per-traversal events** and nothing else:

| Event | What it means | What the slice adds |
|---|---|---|
| **Adoption** — `clock.tick == frame.deadlineTick && phase == frame.startPhase` and `adoptPreparedPlaybackWindow(slot)` succeeds | the eligible boundary: the boundary *may serve the current revision*, because the replacement prepared for this traversal was adopted here | `advancePlaybackLagTraversal(slot, frame.startPhase, span, loop.playbackRevision, /*replacementReady=*/true)` → `Current` → **the episode and any latched fault end here.** This is the production caller the task owes, and it is still the *only* clear site |
| **Expiry** — the frame is `Serving` and `clock.tick >= frame.deadlineTick && clock.tick - frame.deadlineTick >= span` | the traversal this frame promised passed **unserved**: the interval kept its previous revision for that pass | `advancePlaybackLagTraversal(…, /*replacementReady=*/false)` → the first miss is the **allowance** (`one_pass_late`, named once), a second consecutive miss **of the same interval** is the named **breach** (the latch) |
| every validated serving turn | the lane owns this slot's serving | `applyLastResortForSlot(slot, phase)` — one bounded read: `NotNeeded` unless the fault is latched **and** no valid coverage remains |

**The cadence is the frame's, not the turn's.** `PlaybackLagState`'s own rule is *"the allowance is per pass **and per
interval**"* (`coversInterval`/`resetEpisode` on `(intervalStart, intervalLength)`), and a miss is counted per **call**. So
the judgement is called only at a boundary event — never per turn — and the interval identity passed is the frame's own
(`frame.startPhase`, span), which is the interval the frame serves. A second consecutive miss therefore latches when that
interval recurs (a stable interval, or the same aligned boundary on the next pass), and the loop-pass route already
wired through `serviceAdmittedDeadlineMisses` keeps its own count. **Named consequence:** with a sliding span, two
*adjacent* frames are different intervals, so they are different episodes — which is the documented rule, not a gap.

**Placement and cost.** The resolver is FLASH-placed (`src/Track/TrackPlaybackWindowBuild.cpp`), reached from the clock
path by the call that already exists, so **no ITCM call site and no RAM1 variable are added**. `span` is the resolver's
own `constexpr uint32_t span = 2 * Config::TICKS_PER_BAR` (the frame's coverage length). The whole slice lives behind
`#if PLAYBACK_WINDOW_ROLLING_ENABLED`, i.e. in the lane that owns serving.

## 3. Scope, and what is deliberately not added

**In scope:** the two boundary events and the per-serving-turn containment application in
`Track::resolveRollingPlaybackWindow`; nothing else. No new state, no new row family, no new scheduler, no second
authority: the judgement, the containment resolution and the last resort are the existing owners, now called.

**Out of scope (named, not implied):** the container/exception *policy* itself (already measured by 4.5's own fixtures);
displayed provenance (the display's, RWO **S4e**); the device leg (6.4/6.5); the covered-and-serving containment of a slot
the lane is **not** serving (the resolver returns early for an `Idle`/rejected frame, so the judgement is not called
there); 3.7b's fallback removal; and any change to `serviceAdmittedDeadlineMisses`' counting.

**A limit this slice does not hide:** with `PLAYBACK_WINDOW_ROLLING_ENABLED` defaulting to **0**, the added calls are
compiled into the lane-enabled images only (`teensy41-capture-serial-window-qualification`, and this suite, which sets the
macro to 1). The default capture image keeps its exact behaviour and its approved RAM1 line; the lane's own activation
(3.10) is what makes this live, and the qualification env's RAM1 history (I5's record: it is over its own budget and
could not be flashed) stays its own blocker.

## 4. Transition oracle (pinned before the edits)

Driven through the lane's real serving path (`Track::serviceRollingPlaybackWindow` → `stepPlaybackViewMaintenance` →
`resolveRollingPlaybackWindow` via the clock path), on the suite's own demoted-target harness:

| # | Transition | Oracle |
|---|---|---|
| R1 | a **latched** fault, then a frame **adopted** at its eligible boundary | the fault is **cleared** (`deadlineContractFaultLatchedForSlot == false`), `missedTraversals == 0`, and the served window is the **current** revision — recovery through the real consumer, not through a reset |
| R2 | a latched fault, the lane serving a readable window that covers the phase | `applyLastResortForSlot` answers `NotNeeded`: **zero** events on the wire, the served window untouched, the slot's ledger entries alive |
| R3 | a latched fault, and **no** valid coverage remains (the served view is unpublishable/no longer covering) | the last resort fires: exactly that slot's closing note on the wire, a **sibling slot's** sounding untouched, no transport stop, no rebuild |
| R4 | a frame whose traversal passes **unserved** (the requester is not serviced past the deadline) | exactly **one** `one_pass_late` row for that interval and **no** `lag_breach` for a first miss |
| R5 | an adoption boundary that ends the episode | a following miss starts a **fresh** allowance (the count was reset by R1's clear, never inherited) |

## 5. Fixtures and red-proofs

* **Fixture (added):** `test_containment_and_recovery_are_driven_by_the_lane_boundary` in
  `test/test_runtime_working_set_ownership/` (the suite sets `PLAYBACK_WINDOW_ROLLING_ENABLED 1`), reusing the existing
  demoted-target harness (`assert_s4b_7_a_demoted_target_through_the_turn`) and the suite's lane drive
  (`rollingTestTick`) plus `PlaybackPathRows` / `MidiSendCapture` as the oracles.
* **Red-proofs (planned, each restored byte-identically and sha256-checked):**
  * **RP-A** the adoption judgement removed → R1 fails (the fault stays latched across a real recovery);
  * **RP-B** the expiry judgement removed → R4 fails (a traversal the serving path saw pass unserved is never named);
  * **RP-C** the containment application removed → R3 fails (a faulted slot with no coverage is never silenced by the
    approved exception; nothing closes).
  A mutation whose anchor is not unique, or whose restore is not byte-identical, is re-run — not recorded.

## 6. Exit criteria

1. focused `test_runtime_working_set_ownership` green, with the new fixture, and the existing rolling fixtures unchanged;
2. full `pio test -e native` green, no suite ERRORED/FAILED;
3. `pio run -e teensy41-capture-serial` **SUCCESS** with the RAM1 line **byte-identical** (the code is behind the gate
   that image keeps at 0), and `pio run -e teensy41-capture-serial-window-qualification` built and reported **honestly**,
   including whether its own guard passes;
4. the three red-proofs measured red with byte-identical restores;
5. `tasks.md` (4.5 and 3.7a) and `RECONCILIATION.md`'s 4.5 row recorded as an increment, with everything still owed named.

## 7. Risks, and how they are bounded

* **Over-counting (the dangerous direction).** A miss is only ever counted at a boundary event, and the interval identity
  is the frame's own; a per-turn call would latch after two turns and is explicitly not what is done. `Current` is
  idempotent (it resets), so the recovery call cannot inflate anything.
* **Wrong silence.** The last resort is guarded twice inside its own owner: no emission unless the fault is latched, and
  silence only when the served window is unreadable or stops covering the phase — a healthy slot, and a faulted slot that
  still has coverage, emit **nothing** (R2 measures it).
* **Cost.** Three flash calls at events that already exist, one of them per serving turn with an early return.
* **The lane is off by default**, so this slice's behaviour is the lane-enabled images' — stated above rather than
  implied by a green default build.
