# 3.10 — fair, bounded, deadline-ordered service: audit and next slice

**Change:** `windowed-playback-generation` · **Task:** 3.10 (DEC-108 §13b) · **Date:** 2026-10-21 · **Nature:** audit +
scope declaration. No firmware, no test today.

## What the task names

1. a **recurring bounded opportunity** on the existing deferred machinery;
2. **round-robin across eligible holders**;
3. **earliest outstanding playback deadline first** *within* a holder;
4. the **cursor advances even when a holder cannot progress**;
5. capture finalization and persistence keep their **own** opportunities (no starvation);
6. **one declared grain per admitted turn**, no hidden stacking;
7. a fixture where an **early holder is blocked** while later holders need preparation and capture/persistence work is
   pending, asserting a **progress bound for every admitted holder** (including inbound switch preparation and overlapping
   finalization);
8. the **supported envelope derived** from grain costs and the worst permitted service gap.

## What exists today

| Requirement | Status |
|---|---|
| bounded opportunity, existing machinery | **exists** — `DeferredIdleOwner` (Snapshot, VisualCache, wrap-append, Persistence, LoadAndDisplay, SlotProvision, PlaybackViewMaintenance) and the gated polls around deferred remainder |
| round-robin across holders | **exists** — `serviceCanonicalPrefixTurn` (cursor advance per served holder) and `selectPlaybackHolderRoundRobin` used by `src/main.cpp`'s per-track visit |
| deadline order *within* a holder | **exists** — `PlaybackHolder { eligible, canProgress, outstandingDeadline }`, `PlaybackHolderSelection { None, Service, AllBlocked }`, `selectPlayholder` used by the Track owner (`TrackPlaybackWindowBuild.cpp`) |
| cursor advances past a blocked holder | **exists** by construction in both scans (`sawEligible` → `AllBlocked`) |
| capture/persistence keep their own turns | **exists** as separate owners, but **no fixture asserts non-starvation across them** |
| one grain per turn, no stacking | **exists** per owner (`PlaybackViewMaintenance`'s one declared grain), asserted for the window owner only |
| **the fixture 3.10 names** | **MISSING** |
| **the derived envelope** | **MISSING** |

## The gap in one sentence

The *mechanism* is implemented and unit-tested at the holder level (`selectPlaybackHolder` has two RWO cases); the *composition*
the task actually asks for — blocked early holder, later holders needing preparation, capture/persistence pending, a progress bound
for **every** admitted holder — has no fixture, and therefore the envelope it is supposed to derive has never been stated.

## Next slice (declared scope)

**One fixture, test-only, in `test_runtime_working_set_ownership`:**

- three or more holders where holder 0 is **permanently blocked** (its admission cannot progress) and holders 1..n each hold
  owed window work;
- a capture finalization and a persistence owner both pending in the same window;
- drive the production service entry for a bounded number of turns and assert: every non-blocked holder progresses at least
  once per `n` turns (the round-robin bound), the blocked holder never consumes a service slot permanently, and capture and
  persistence each complete inside their own bound;
- derive the **envelope** from the measured grain costs: `worst permitted gap ≥ holders × grain` and state it as a bound in
  turns, not microseconds (no invented µs budget).

**Out of scope for that slice:** any change to the grain size, the deadline model, `ExecutionBounds.md` rows, or the admission
envelope; no device claim (the service/timing device legs stay with 6.3/6.5/3.15f).

**Why this ordering:** `3.15f` names 3.10 as a gate, and the committed-window removal proof left open by 3.15c needs a
production-shaped frame — which is what this fixture's holder set provides.

## The composition fixture landed (2026-10-21)

`test_3_10_a_blocked_early_holder_cannot_starve_the_rotation` (RWO, default gate, **234/234**) pins the three outcomes and the
fairness bound:

| Assertion | What it pins |
|---|---|
| a blocked holder is **ineligible** and never takes a turn, while its sibling's deadline is the earliest of all | urgency cannot buy a turn for work that cannot progress |
| over 12 selections with 4 holders, each progressable holder is served **exactly 4** times | round-robin holds with a blocked member present; the bound is `count` selections per holder |
| all owed holders blocked → `AllBlocked`, cursor still advances, no holder named | "blocked" is named, not silently skipped |
| nothing owed → `None` | distinct from `AllBlocked` |

**The envelope, stated in turns:** every holder that can progress is served within `count` consecutive selections
(`count = holders`), so the worst permitted service gap per holder is `count` turns — no microsecond budget is invented, and grain
cost enters only through how many turns a grain needs.

### A regression this fixture caught on the way

The first form applied **`selectPlaybackHolder`** (the *within-holder* rule) across holders and one holder took **all 12 turns** —
exactly the starvation the header comment says was corrected on 2026-10-02 by adding `selectPlaybackHolderRoundRobin`. So the two
rules are easy to confuse at a call site, and the failure is silent (no assertion, just one holder monopolising). The composition
fixture now pins the correct pairing: **`selectPlaybackHolder` within a holder** (deadline order, `AllBlocked`/`None` naming) and
**`selectPlaybackHolderRoundRobin` across holders**.

### Still owed by 3.10

The driven version — holders that owe **window grains**, a pending capture finalization and a pending persistence in the same window,
asserting completion bounds through the production service entry rather than through the selection functions. That is the half the
scope declaration named, and the present fixture is its pure-function foundation.

## The driven half — first attempt measured, withdrawn (2026-10-21)

A driven variant was built: two tracks each owing a window grain (`prepareOwingWindowHolder`, 32-bar loop), the production
selection asked every turn, the chosen track stepped. Two readings, both useful:

| Attempt | Reading |
|---|---|
| 12-bar loop, bars 25–27 | the helper's own root publish refuses (a helper-level assertion, not the scheduler) |
| 32-bar loop | rotation stepped one track **3** times and the other **1** — because a track's **offer stops being eligible** as soon as its grain completes |

So the driven bound is not `count` turns flat: it is `count` turns **per still-owing holder**, and the offer's eligibility is the
state that moves. The pure fixture states the bound where it is exact (a fixed holder set); the driven fixture needs the offers'
lifecycle modelled — which is the real content of 3.10's "progress bound for **every admitted holder**".

Fixture withdrawn rather than left red; the measurement is recorded so the next attempt starts from the eligibility transition
rather than from the turn count.
