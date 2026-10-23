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

## The driven half landed, lifecycle-aware (2026-10-21)

`test_3_10_each_still_owing_track_is_served_within_count_eligible_turns` (RWO, default gate) models the transition the previous
attempt missed: two tracks owe window grains, the production selection is asked every turn, and the bound is asserted **per turn on
which a track was actually eligible**:

- `eligibleTurns[i]` counts the turns the track's offer was eligible;
- `servedTurns[i]` counts the turns it was served;
- the assertion is `servedTurns[i] >= eligibleTurns[i] / kTrackCount`, i.e. **one service per rotation of the still-owing set** —
  a track that has served its grain drops out and cannot be double-counted as owed, which is exactly what made the flat `count`-turn
  form fail.

| Fixture | What it pins |
|---|---|
| `test_3_10_a_blocked_early_holder_cannot_starve_the_rotation` | fixed holder set: blocked holder never served, each progressable holder once per rotation, `AllBlocked`/`None` named |
| `test_3_10_each_still_owing_track_is_served_within_count_eligible_turns` | live offers: the bound holds **per still-owing holder**, with eligibility moving under it |

**3.10's fixture requirement is now met in both halves.** What remains for the task's own closeout: the pending **capture finalization
and persistence** work in the same window, and the envelope stated against grain costs (the turn bound is stated; the grain-cost
derivation is not), plus the device/service legs owned by 6.3/6.5.

## The non-starvation clause landed (2026-10-21)

`test_3_10_capture_and_persistence_keep_their_own_opportunities` pins the structural half of the clause:

- **every `DeferredIdleOwner` is distinct** (Snapshot, VisualCache, wrap-append, Persistence, LoadAndDisplay, SlotProvision,
  PlaybackViewMaintenance), so no turn can be mistaken for another owner's;
- **`serviceCanonicalPrefixTurn` declines every non-canonical owner** — persistence, playback maintenance, snapshot, slot provision and
  load/display cannot consume the canonical prefix turn, so their cadence is not spent here;
- **the canonical rotation serves every track once per `trackCount` turns** (6 turns / 3 tracks → exactly 2 each), which is the
  non-starvation property for the canonical prefix.

**What this is and is not:** it is a **structural** proof — distinct opportunities and a rotation that cannot be absorbed. It is not a
driven finalization/persistence completion bound; that needs real storage work in the same window and remains owed, together with the
grain-cost envelope and the 6.3/6.5 device/service legs.

## The driven composition landed (2026-10-21) — 3.10's fixture text, executed

`test_3_10_the_owner_rotation_carries_a_blocked_holder_finalization_and_persistence` lives in
`test_shared_capture_finalization` — the one suite that links the **real** capture-stop commit, the **real** storage
closure and the full Track harness, so the other owners can be *executed* rather than counted (the runtime-working-set
suite cannot link `processDeferredIdleMaintenance`: it pulls the storage closure). It drives the composition
`src/main.cpp` runs, one turn at a time:

| Step | Production call |
|---|---|
| owner of the turn | `MidiServiceDrain::selectDeferredIdleOwner` (the 7-owner rotation) |
| `PlaybackViewMaintenance` | every track's `playbackWindowServiceOffer` → `selectPlaybackHolderRoundRobin` → one grain through `stepPlaybackViewMaintenance` |
| `Snapshot` / `VisualCache` / `VisualCacheWrapAppend` | the finalizing track's `processDeferredIdleMaintenance`, gated by `hasRealtimeDeferredIdleWork()` exactly as `main.cpp` gates it |
| `Persistence` / `LoadAndDisplay` / `SlotProvision` | `serviceDeferredPersistenceTurn<StorageManager>` (storage owns it) and nothing for the other two — `main.cpp` never routes a track's maintenance through them |

The window carries, simultaneously: track 1 **blocked** (a committed demand retained in an earlier clock epoch — the
obligation survives, only its position is void); tracks 2 and 3 owing **real admitted window builds**; track 0 the
**selected, playing** track with a **real accepted overdub stop** in flight, whose sealed capture chunks are the
persistence obligation. Measured: **403 turns / 57 rotations**, 57 lane opportunities, **14** lane grains served across the two
windows, the finalization advanced **29** times with a worst gap of **7 turns** (one rotation — tight
against the declared bound), persistence did work on **268** turns with **0** owed-but-unserved turns in either admission
class, the blocked holder was **offered on all 57** lane opportunities and served **0**, and both windows, the
finalization and the persistence work completed. Every owner kept exactly one turn per rotation.

**Red proofs (production mutations, restored byte-identically):** ignoring `canProgress` in
`selectPlaybackHolderRoundRobin` serves the blocked holder 43 times and fails its own assertion; dropping the
`queuedCaptureChunks` term from `serviceDeferredPersistenceTurn` fails "the persistence obligation completes too";
removing the handoff margin from `canCompleteWithinLead` fails the envelope fixture's "refuses one tick below it".

**Not red-proved, recorded:** the finalization's *completion* assertion was verified sensitive by a harness-only check
(a 40-turn cap fails it), because the production mutations tried were either non-discriminating (dropping
`hasPendingDeferredOverdubStop()` from `hasRealtimeDeferredIdleWork` leaves the track reachable through its audible
state) or stalled the whole suite (a no-op stop grain).

**Named limits:** *inbound switch preparation* is covered only as its own `SlotProvision` turn in the rotation (a
pending switch is not driven here; S4b.6/S4b.7 own the driven provisioning). The persistence **save/revision** class's
completion is the storage suite's `test_i4_revision_only_save_dispatches_and_completes_through_main_admission` — this
harness reports `bootRecoveryBlocked()`, so a workspace save cannot even be requested there; what completes here is the
sealed-chunk class, and both classes' admission rules are asserted. No device legs (6.3/6.5), and the rolling
fetch/preview cadences are not driven (they have their own fixtures).

## 3.10 status after this run

| Term | State |
|---|---|
| recurring bounded opportunity | implemented; pinned structurally |
| round-robin across eligible holders | **pinned** (fixed set + live offers + driven composition) |
| earliest deadline within a holder | pinned by the holder-level cases |
| cursor advances past a blocked holder | **pinned**; the driven fixture serves it 0 times while offering it every turn |
| capture/persistence own opportunities | **driven completion landed** (finalization + sealed-chunk persistence), with the save class cited |
| one grain per turn | pinned for the window owner (existing) |
| the fixture 3.10 names | **landed** (blocked holder + later preparation + finalization + persistence in one window) |
| derived envelope | **stated correctly and instantiated** from the live contract, two-sided at the admission boundary |
| device/service legs | 6.3 / 6.5 (not this task's) |

**Task 3.10 is closed** on its own text. The withdrawn envelope form, its correction, the two fixtures and their red
proofs are recorded above; `tasks.md` carries the same closeout.

## The envelope, derived from the contract's own terms (2026-10-21)

3.10 asks for the supported envelope to be *derived* from grain costs and the worst permitted service gap, with **no invented
microsecond budget**. The terms are all declared in `CoverageServiceContract` (`include/RuntimePlaybackViewBuild.h`):
`requiredFillGrains`, `requiredOrderGrains`, `requiredPromotionGrains`, `maximumConsecutiveDeniedTurns`,
`maximumPhaseAdvancePerTurn`, `leadTicks`.

**The derivation.** A holder needs `G = fill + order + promotion` grains to complete one window. With `H` holders served
round-robin, a holder's own turns are `H` turns apart *by construction* (the rotation bound the fixtures now pin), so
between two of its turns the playhead advances at most `H × maximumPhaseAdvancePerTurn`. Completing `G` grains therefore
needs at most `G × H` turn-gaps of phase advance, and that, **plus the handoff margin the admission keeps for itself**,
must fit the lead the request actually has:

```
leadTicks + G x H x maximumPhaseAdvancePerTurn  <=  available     (available = handoffLeadTicks(phase, intervalStart, len))
```

solved for the work the lead can carry: `G × H × maximumPhaseAdvancePerTurn ≤ available − leadTicks`.

**Correction (2026-10-21, landed with the driven half).** This section previously stated
`(G × H + (H − 1)) × maximumPhaseAdvancePerTurn ≤ leadTicks ≤ handoff − margin`. That form is wrong in two ways and is
withdrawn: it adds a **second** rotation on top of `G × H` (the first grain's worst wait is already inside `G × H`), and
it reads `leadTicks` as the *available* lead when the contract uses it as the **handoff margin** that is subtracted from
it. The form above is the one both `CoverageServiceContract::canCompleteWithinLead` and the fixtures use; no constant
changed, only this statement.

**Consistency with the admission the code already enforces.** The same statement appears in the admission's own arithmetic
(`grains × (denied + 1) ≤ (available − leadTicks) / maximumPhaseAdvancePerTurn`, i.e.
`grains ≤ (available − leadTicks) / (H × Δ)`), with `denied + 1 = H` the across-track rotation. No new constant, no new
budget, and nothing measured in microseconds.

**What is derived vs. what is measured.** The *turn* bound (`count` per still-owing holder) is measured and pinned by
`test_3_10_each_still_owing_track_is_served_within_count_eligible_turns`. The *tick* form is now instantiated numerically
**from the live contract and the live window**, not copied: the `H × K` contention sweep composes it
(`contentionLoopBarsFor` / `contentionOutsideLoopBarsFor`, and a two-sided `canCompleteWithinLead` check), and
`test_3_10_the_declared_envelope_is_the_admissions_own_boundary` reads one real gathered window's own cost and the live
terms, then shows the **production request path** admitting at exactly `lead + G × H × Δ` and refusing
`CannotCompleteBeforeHandoff` one tick below it — 12 gathered entries cost 4 grains, and with `H = 8`, `Δ = 64`,
`lead = 768` the boundary is **2 816 ticks** (admitted at 2 816, refused at 2 815, and the retained promise at the
boundary is exactly 2 816).

**Still owed:** nothing of the task's own text. The driven completion bound landed above; the device/service legs are
**6.3 / 6.5's**, and they are what keeps the envelope's *declared* figures (Δ = 64, the one-bar margin) from being
device-qualified here.
