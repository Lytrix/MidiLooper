# G2 execution packet — 4.5: the lane's adoption boundary is the recovery point

Slice of task **4.5** (its **recovery half**) on task **3.7a**'s serving path. Written before any edit, per the
[G2 execution packet](../../../../Authority/IMPLEMENTATION_READINESS.md#g2-execution-packet--required-for-each-implementation-slice)
requirement. **Revised after one independent review withdrew two thirds of the first form** — §2, §4 and §5 keep the
withdrawn items visible rather than deleting them, and §8 is the revision record. The
[EVIDENCE.md](EVIDENCE.md) that pairs with this packet reports the *landed* slice.

## 1. Why this slice, and why now

4.5's own OWED list still names *"production containment/recovery … eventual recovery through the real consumer"*, and
`RECONCILIATION.md`'s row *4.5 containment, last resort, closures* states the gap in the same words: *"positioned promises
now have a gated rolling requester, but containment/recovery is not integrated into that serving slice"*. Task 3.7a's text
requires 4.5 to be finished *"against the real handoff, including their production-path interleavings and
closure/recovery gates"*; the closure half landed as I9, so this is the remaining half.

**Measured, not assumed (grep, before the packet):** `Track::advancePlaybackLagTraversal`, `Track::containmentForSlot` and
`Track::applyLastResortForSlot` had **no production caller at all** — `src/` contained only their definitions, and the only
callers anywhere were tests. The consequence that *this* slice repairs:

* the latched **deadline-contract fault never clears in production** — its only clear site is
  `advancePlaybackLagTraversal`'s `Current` branch (`lag.missedTraversals = 0; lag.breachReported = false;
  runtime->deadlineContractFaultLatched = false;`), so a fault latched through the one route that *is* wired (an admitted
  promise whose deadline passes, `serviceAdmittedDeadlineMisses` → `noteMissedTraversalForSlot`) is permanent.

The second consequence — the approved last resort is unreachable, because nothing consults `containmentForSlot` — is
**named here and not repaired** (see §3 and §8): the review showed this resolver is not an owner that may apply it.

## 2. Where the boundary is, and why it is there

DEC-108's owner for this is the lane's own serving path (item 1: *"one window-build/publication owner:
`Track::stepPlaybackViewMaintenance`"*; item 13(a): *"**Pass = one traversal of the affected interval in the playback
projection**"*). The lane keeps exactly that per-traversal frame — `LoopPlaybackRuntime::RollingFrame` (`startPhase`,
`loopLength`, `deadlineTick`, `phase`) — and `Track::resolveRollingPlaybackWindow` has one per-traversal event that can end
an episode:

| Event | What it means | What the slice does |
|---|---|---|
| **Adoption** — `clock.tick == frame.deadlineTick && phase == frame.startPhase` and `adoptPreparedPlaybackWindow(slot)` succeeds | the eligible boundary: the boundary *may serve the current revision*, because the replacement prepared for this traversal was adopted here | `advancePlaybackLagTraversal(slot, frame.startPhase, span, loop.playbackRevision, /*replacementReady=*/true)` → `resolvePlaybackLag` returns `Current` → **the episode and any latched fault end here.** This is the production caller the task owed, and it is the *only* one |

**Why this is a real recovery and not a reset:** the call runs only after the resolver has verified the adopted view
(`complete`, `published`, matching `storageGeneration`, matching `generation`, `origin == Committed`, coverage equal to the
frame's own span/loop length/start phase) and `adoptPreparedPlaybackWindow` succeeded — i.e. the serving path is serving the
frame's own freshly adopted replacement. **Named honestly:** with `replacementReady=true`, `resolvePlaybackLag` short-circuits
on its `replacementReady` branch and never reads `servedIsCurrent`, so the call at this site is a **statement** ("this
boundary may serve the current revision") rather than a discriminator; `desiredRevision` is passed for the record, not
branched on. That is the intended semantics of the eligible boundary, and it is what the red-proof measures: removing the
call is what makes the fixture fail.

**Cost and placement.** The resolver is FLASH-placed (`src/Track/TrackPlaybackWindowBuild.cpp`), reached from the clock path
by the call that already exists, so **no ITCM call site and no RAM1 variable are added**. `span` is the resolver's own
`constexpr uint32_t span = 2 * Config::TICKS_PER_BAR`. The call lives behind `#if PLAYBACK_WINDOW_ROLLING_ENABLED`.

### Withdrawn neighbours (visible, because the withdrawal is the result)

The first form of this slice also wired two more sites. Independent review falsified both; they are **not** landed:

| Withdrawn site | Why the premise fails |
|---|---|
| the **expiry** judgement: `advancePlaybackLagTraversal(…, /*replacementReady=*/false)` when a `Serving` frame's traversal passed (`clock.tick - frame.deadlineTick >= span`) | at that moment the phase has advanced a whole span past `frame.startPhase`, so the interval named was the one the frame had just **served** (at worst one pass late) and not the traversal that went unserved. `PlaybackLagState` resets the episode when the interval changes, so the call minted the allowance for the wrong interval; the promise route (`serviceAdmittedDeadlineMisses`) already counts that miss with the demand's own identity |
| the **last resort** at the resolver's three `Serving`-phase rejections | those rejections are exactly the turns the **retained merged fallback** takes the slot over, so `applyLastResortForSlot` there closes a sounding that another serving route is about to play. `containmentForSlot`'s "no valid coverage remains" is defined on the published **window**; this resolver's rejection condition is not that predicate, and its reachability was not established (only one of the three sites is reachable at all) |

## 3. Scope, and what is deliberately not added

**In scope:** one call at the adoption boundary in `Track::resolveRollingPlaybackWindow`; nothing else. No new state, no new
row family, no new scheduler, no second authority: the judgement is the existing owner, now called.

**Out of scope (named, not implied):** the **last resort / containment application** (withdrawn — see §2); the
container/exception *policy* itself (already measured by 4.5's own fixtures); displayed provenance (the display's, RWO
**S4e**); the device leg (6.4/6.5); 3.7b's fallback removal; and any change to `serviceAdmittedDeadlineMisses`' counting
(it shares the episode counter — §7).

## 4. Transition oracle (pinned before the edits; reduced to the landed slice)

Driven through the lane's real serving path (`Track::serviceRollingPlaybackWindow` → `stepPlaybackViewMaintenance` →
`resolveRollingPlaybackWindow`, off the clock path via the suite's `rollingTestTick`):

| # | Transition | Oracle |
|---|---|---|
| R1 | a **latched** fault, with **no** lane promise outstanding, then a frame **adopted** at its eligible boundary | the fault is **cleared** (`deadlineContractFaultLatchedForSlot == false`), the episode counter is back to **0** (`lagState.missedTraversals`), the served window is the **current** revision — recovery through the real consumer, not a reset |
| R2 | after R1, a serving turn with a sounding in the slot's ledger | **zero** events on the wire; the ledger entry is still alive; **no** `last_resort_slot_silence` row (the last resort is not wired from this resolver at all) |
| R3 | after R1 | **no** `lag_breach` row and no closure is attributed to the recovery |

**Withdrawn oracles (they belong to the withdrawn sites, and are kept here so the reduction is auditable):** R2 of the first
form (the last resort's `NotNeeded` control), R3 (the last resort firing: one closure for the affected slot, a sibling
untouched, no transport stop, no rebuild) and R4 (the expiry naming exactly one `one_pass_late` row).

## 5. Fixtures and red-proofs

* **Fixture (added):**
  `test_the_lane_boundary_is_the_production_caller_that_recovers_a_latched_fault` in
  `test/test_runtime_working_set_ownership/` (the suite sets `PLAYBACK_WINDOW_ROLLING_ENABLED 1`). **Self-contained**: it
  builds its own 24-bar demoted loop in slot 0, publishes its root, prewarms, forces `TRACK_PLAYING`, establishes a
  **served window** through the window lane's own `requestPlaybackWindow` → `stepPlaybackViewMaintenance` → served-window
  path, opens the lane's demand with `serviceRollingPlaybackWindow`, and then drives with the suite's `rollingTestTick`.
  The S4b.7a harness was **not** reused (it produced a trailing failure plus an ERRORED suite — I6/I9's record, re-measured
  here).
* **Red-proof (one, matching the one landed call):** **RP-A** — the adoption judgement removed
  (`advancePlaybackLagTraversal` call deleted at the adoption boundary) → R1 fails. Each mutation's restore must be
  **byte-identical**, checked by `diff` against a pre-mutation copy *and* by sha256. A mutation whose anchor is not unique,
  or whose restore is not byte-identical, is re-run — not recorded.
* **Void:** RP-B (the expiry judgement removed) and RP-C (the containment application removed) measured the **withdrawn**
  sites and no longer describe this slice.

## 6. Exit criteria

1. focused `test_runtime_working_set_ownership` green, with the new fixture, and the existing rolling fixtures unchanged;
2. full `pio test -e native` green, no suite ERRORED/FAILED;
3. `pio run -e teensy41-capture-serial` **SUCCESS** with the RAM1 line **byte-identical** (the call is behind the gate that
   image keeps at 0), and `pio run -e teensy41-capture-serial-window-qualification` built and reported **honestly**,
   including whether its own guard passes;
4. the red-proof measured red with a byte-identical restore;
5. `tasks.md` (4.5 and 3.7a), `RECONCILIATION.md`'s 4.5 row, `CURRENT_WORK.md`, `PROJECT_STATE.md` and the
   `G2-EXECUTION-PACKET.md` increment note recorded as an increment, with everything still owed named — including the
   **withdrawn** items and the durability documents that had claimed them.

## 7. Risks, and how they are bounded

* **The episode counter is shared.** `advancePlaybackLagTraversal`'s `Current` branch resets `missedTraversals`,
  `breachReported` and the latch — all of which `noteMissedTraversalForSlot` also writes for the **promise** route's misses
  (`serviceAdmittedDeadlineMisses`). So an adoption resets state the promise route fed. This is the episode rule (an
  eligible boundary ends the episode), and it is recorded rather than glossed: the earlier draft's claim that "the loop-pass
  route keeps its own count" was **wrong** — that route's per-promise dedup counter is its own, the episode state is not.
  Bounded by: the call runs only at a real adoption, and `serviceAdmittedDeadlineMisses` is unchanged.
* **Wrong silence:** not applicable in the landed form — no silencing path is wired from this resolver (this is precisely
  what the withdrawal bought).
* **Over-counting:** impossible in the landed form — the call's only outcome is `Current`, which resets.
* **The lane is off by default**, so this slice's behaviour is the lane-enabled images' — stated rather than implied by a
  green default build.

## 8. Revision record (what the review changed, and why it is kept)

Committed **`eefd90a5`** contains the withdrawn form: the expiry judgement, the last-resort application at the three
`Serving`-phase rejections, a fixture that drove all of it, and two further red-proofs (RP-B, RP-C). One independent review
falsified it with six findings — **F1** wrong silence (verified), **F2/F5** the containment sites' reachability not
established, **F3** the expiry call could also clear (so "adoption is the only clear site" was false while it existed),
**F4** the expiry named the interval already served (verified), **F6** the fixture manufactured an unreachable state — plus
the shared-counter over-claim corrected in §7. The landed slice is the **adoption call only**, its fixture reduced
accordingly, and its red-proof set reduced to RP-A. The withdrawn calls had cost **+144 B of ITCM** in the lane-enabled
image; withdrawing them returned that image to its pre-slice line byte-for-byte. The findings and the numbers are in
[EVIDENCE.md](EVIDENCE.md) §3 and §4.
