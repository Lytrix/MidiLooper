# 3.15e — migration slice 2a: completion record

**Packet:** [`EXECUTION-PACKET.md`](EXECUTION-PACKET.md) · **Status:** **native verified**, no production include, no
audible change, no DEC yet (slice 3 owns the emission rule).

## Landed

`include/Utils/PlaybackDemandArbitration.h` — a pure owner-level resolver over the three demands that can owe the one
`ready` role, with a contractual class order (Preview > Overlay > Committed), identity-before-class refusal, exactly one
owner, a named fate for every other claimant, a named role conflict, a named role-capacity refusal that says the
compatibility route is authoritative, and no stored state at all.

New suite `test/test_playback_demand_arbitration` (8 cases) covers the four owner constraints directly: deterministic
priority (also independent of the caller's claim order), explainable losers (deferred / superseded / stale / role
conflict), identity beating class for both overlay and preview, and the single-owner + no-extra-state rule.

## Gates

| Gate | Command | Result |
|---|---|---|
| Focused | `pio test -e native -f test_playback_demand_arbitration` | **8 / 8** |
| Full native | `pio test -e native` | **3 394 / 7 skipped / 3 387 succeeded / 0 failed** (baseline 3 386 / 7 / 3 379) |
| Default build | `pio run -e teensy41-capture-serial` | SUCCESS, `code 391 688 + padding 1 528` — unchanged |
| Lane build | `pio run -e teensy41-capture-serial-window-qualification` | SUCCESS, `code 391 848 + padding 1 368` — unchanged |
| Production include | `grep -rn PlaybackDemandArbitration src include` | only the header itself: no firmware TU includes it |
| Spec / whitespace | `openspec validate --strict`, `git diff --check` | valid / clean |

## Mutations (each restored byte-identically, header `sha256 7dce9cfaed44fad3…`)

| # | Mutation | Result |
|---|---|---|
| M1 | class order reversed (Committed > Overlay > Preview) | **6 failures**, each naming its contract case |
| M2 | `identityCurrent` ignored by the validity test | exactly the two stale-identity cases fail |
| M3 | role-conflict branch disabled | exactly the two-complete-candidates case fails (`Expected 5 Was 2`, `RefusedRoleConflict` refused) |

## Owed (slice 2b)

Wire the **claim providers** and the role-retirement calls: the committed requester, the NOTE_EDIT preview lane and the
overlay lane each build their claim from existing owner state (preview revision; slice-1 `overlayCaptureGeneration()`;
committed request identity), the resolver decides who may build/adopt this turn, and the loser returns its named outcome
without recording a second complete candidate. That slice must re-check the pins and the `ready` contention under a
NOTE_EDIT session that starts and exits while an overdub stays live. Slice 3 then defines the clock edge, the echo
removal and the ledger rule (DEC).

## 2b-i — the per-class identity providers (2026-10-22)

`include/Utils/PlaybackDemandClaims.h` turns each owner's two existing facts into the claim the arbiter consumes, and it
is where the "identity beats role labels" rule is actually true or false:

| Class | Live / bound pair | Current only when |
|---|---|---|
| Committed | `playbackRevision` + Track playback generation | both are present and both equal the bound pair |
| Preview | NOTE_EDIT session preview revision | the live session revision is non-zero and equals the bound revision |
| Overlay | live overdub capture content generation (slice 1's `overlayCaptureGeneration()`) | the live generation is non-zero and equals the bound one — a wrap's pass commit advances it |

Every comparison is within one class, and **absent is never current** (`live == 0`), so "no root yet", "no session" and
"no live capture" cannot accidentally match a zero bound value and claim a boundary.

Four new cases (12/12 in the suite): the per-class rules including the absent case; a wrap pass commit making the
overlay's own claim stale so the **committed** successor wins despite the class order; the converse (an advanced committed
root stale against a valid overlay); and the three-view pressure case — three valid demands, one `ready` role, exactly one
owner, the role's fate decided by name, two losers holding nothing.

Two mutations, each killing exactly the intended case and restored byte-identically (`sha256 5bb3a810eb7c079b…`):
dropping the non-zero guard (the absent-currency case fails), and dropping the generation half of the committed
comparison (an advanced generation reads as current).

Gates: focused **12/12**; full native **3 398 / 7 skipped / 3 391 succeeded / 0 failed**; both images byte-identical at
the pins (`391,688 + 1,528` / `391,848 + 1,368`) because no firmware TU includes either header.

### Still owed in slice 2

1. **2b-ii — the overlay lane.** A per-overdub request identity and an owner turn that builds slice 1's continuation
   window for the active overdub generation (admission, role, retire on pass commit). Nothing emits from it yet.
2. **2b-iii — the wiring.** Those lanes call the resolver with their own claim plus the other two providers; a loser
   returns a named `PlaybackViewRequestResult`/turn outcome, and the winner executes `supersedeReady`/`retireStaleReady`
   against `viewSet->lifecycle` (the retirement that slice 1 proved is needed). The committed lane must be deferred only
   once the overlay lane can actually take the boundary, otherwise convergence stalls.

## 2b-ii — the per-overdub overlay demand (2026-10-22)

`Track::overdubOverlayDemandForSlot(slot, playheadPhase)` (`TRACK_COLD_MEM`, flash) is the overlay lane's **request
identity**, and it is deliberately a *reading* rather than a record — a stored demand is exactly the hidden fourth state the
arbitration contract forbids, and a reading cannot go stale in storage:

| Field | Derived from |
|---|---|
| `owed` | `isOverdubbing()` && `capture.phase == Overdub` && the live store is non-empty && `baseCommitted` |
| `baseCommitted` | `hasCommittedPasses()` && `loopLengthTicks != 0` |
| `coverage` | the committed route's **own** policy — `Track::playbackWindowInterval(loopLength, demoted, playheadPhase)` |
| `captureGeneration` | `Loop::captureContentGeneration()` (the same value the arbitration's overlay claim compares) |
| `playbackRevision` | `Loop::playbackRevision` |

Fixture `test_3_15e_slice2b_the_overlay_demand_is_the_live_generation_and_the_committed_interval`
(`test_shared_capture_finalization`, the suite with the real edge and the real stop): not-overdubbing → not owed but base
present; a slot with no committed content → no base, no demand; a live session with no rows → not owed; with rows → owed
with the live generation, the current revision and **exactly** the committed policy's coverage, and the
`PlaybackDemandClaims::overlay` claim built from it is current; the **wrap's pass commit** advances the generation, so the
demand taken a moment earlier is stale **by identity** (the claim goes stale with `readyComplete` true, which asks for the
role's retirement) and the re-based store owes nothing; a **stopping** session owes nothing.

Mutation (`sha256 1b0e548caf12ec3f…`, restored byte-identically): the demand's generation replaced by `playbackRevision`
fails exactly this case (`Expected 10 Was 1`). **Named unproven guard:** the `isOverdubbing()` term is not independently
discriminated by the fixture, because a deferred stop has already moved the rows to the job (store empty); it is kept for
the denied/latched-stop shape and recorded here rather than claimed as proven.

Gates: focused **91/91**; full native **3 399 / 7 skipped / 3 392 succeeded / 0 failed**; both images byte-identical at the
pins (`391,688 + 1,528` / `391,848 + 1,368`). Nothing consumes the demand yet and nothing is audible.

### Still owed in slice 2

1. **2b-ii-b — the overlay owner build.** The owner turn that admits a role and drives slice 1's continuation window for
   this demand (committed base for `coverage` + live overlay, retire on generation advance), still emitting nothing.
2. **2b-iii — the wiring.** Lanes ask the resolver; losers return named outcomes; the winner executes
   `supersedeReady`/`retireStaleReady`. The committed lane may only be deferred once 2b-ii-b can take the boundary.

## 2b-ii-b (guard) — the overlay half requires a provenance-bearing base (2026-10-22)

Starting the owner build exposed a decision that has to be made **before** any base source is chosen: a committed base
copied from a raw array (`RuntimePlaybackViewFill::begin(destination, const MidiEvent*, ...)`) carries **no per-row
provenance**, so the one provenance table would silently read "committed pass 0" for every base row. That is the difference
between a contract and a guess, so the continuation now **fails closed**:

- `RuntimePlaybackViewFillRefusal::ContinuationBaseWithoutProvenance` (the refusal set is now five names, packed in three
  bits of the same one byte — the runtime pins are unchanged);
- `beginLiveContinuation` refuses unless `destination.passOriginsReady`, which only the `RuntimeCompanionPasses` (or
  snapshot-cursor) fill produces.

Fixture `test_3_15e_slice2b_a_base_without_provenance_cannot_take_an_overlay_half` (`test_playback_prewarm`) builds a base
from a raw array, proves it is complete and **without** provenance, then requires the attach to be refused by that name,
`overlayContinuationActive()` false and the base window itself still complete (the refusal is about the continuation, not
about the content). Mutation (guard removed) fails exactly this case: header `sha256 cbb3167e6f14b1af…`, restored
byte-identically.

**Consequence for 2b-ii-b, now the explicit blocker:** the overlay lane's committed base must come from a
provenance-bearing source — the canonical `RuntimeCompanionPasses` form (already what the committed lane prepares) or a
pinned `LoopSnapshot::EventWindowCursor`. A cheap `copyEffectiveCommittedEventsInRange` copy is **not** admissible as the
base for a window whose rows must be ledger-classified. Choosing and driving that source (grains, role, retirement on the
pass commit) is the owner build itself, not a wiring detail.

Gates: focused **244/244** (prewarm), full native **3 400 / 7 skipped / 3 393 succeeded / 0 failed**, both images
byte-identical at the pins (`391,688 + 1,528` / `391,848 + 1,368`). Still nothing audible.

## 2b-ii-b — measured blocker before the owner build: the canonical base is **not ready** during an overdub (2026-10-22)

Owner direction: take `RuntimeCompanionPasses` as the committed base (it is the committed lane's canonical form and the only
provenance-bearing base the continuation accepts), and scope the owner build as *"the overlay demand does not create a new
kind of committed base; it requests the same provenance-bearing committed interval the ordinary committed lane would prepare,
then continues that same window with the generation-qualified live overlay."*

Before writing the lane, the prerequisite was measured in the suite that has a real overdub
(`test_shared_capture_finalization`, `test_3_15e_slice2b_…`, live overdub with rows):

```
canonicalReady=0  passCount=0  companionApply=1  frozenPassesIdle=1
```

So in the overdub state the runtime's **canonical pass inputs are empty**: there is no `RuntimeCompanionPasses` base to fill
from until `LoopPlaybackRuntime::stepCanonicalPrefix` has prepared them. The companion apply and the frozen companion
passes are available/idle (the storage exists), but the canonical prefix owner has not been driven.

This changes the lane's shape, not its contract. The lifecycle the owner drew now has an explicit **first** step that costs
grains and shares an owner:

```
overdubOverlayDemandForSlot
        ↓
canonical prefix prepared (stepCanonicalPrefix — the committed lane's own grains)   <-- measured NOT READY
        ↓
RuntimeCompanionPasses fill  (passOriginsReady)
        ↓
base complete → beginLiveOverlayContinuation → append-stable Capture continuation → ready
```

Two consequences the next step must decide, because the overlay lane cannot simply "request the committed interval" without
touching a shared resource:

1. **Shared canonical owner.** The committed lane already drives `canonicalPassInputs`. If the overlay lane drives it too,
   two lanes advance one owner in the same turn — the exact "two drivers, one owner" shape the arbitration is meant to
   prevent. The overlay lane should therefore treat canonical readiness as a **precondition** (`WaitingCanonicalBase`) and
   let the committed lane's grain establish it, or the arbitration must name a single canonical-prefix driver per slot.
2. **Which identity keys the base.** `RuntimeCompanionPasses::begin(...)` binds `canonicalPassInputs`'s generation, so the
   base half's identity is that generation plus the loop revision; the candidate's retirement must key on both (committed
   revision change → base no longer authoritative; capture generation advance → live half stale; coverage change → candidate
   no longer answers the request), exactly as the owner listed.

No code changed for this entry; the probe was removed byte-identically (`git diff` clean) and the focused suite is back to
**91/91**. This is the measured input the owner build (and 2b-iii's arbitration) must consume: the overlay lane's first turn
is a *waiting* state, not a build, until the canonical prefix exists.

## 2b-ii-b — the liveness RED: `WaitingCanonicalBase` would be a permanent wait today (2026-10-22)

The owner locked the invariant *"overlay demand may observe canonical-prefix readiness, but must never advance
canonical-prefix construction"* and asked for the liveness proof before the lane lands: under **overdub-only** demand, does
the **committed** owner still receive canonical-prefix work? Measured in `test_shared_capture_finalization` on the live
overdub, driving 400 real owner turns (`Track::stepPlaybackViewMaintenance`), with no committed demand of its own:

```
before turns  canonicalReady=0  passCount=0  pendingDemand=0
after  turns  canonicalReady=0  passCount=0  pendingDemand=0
```

**RED.** The committed lane is only offered work when it has an owing holder, so with overlay-only demand nothing advances
`canonicalPassInputs` at all. A `WaitingCanonicalBase` state would therefore be a *perfectly legal permanent wait* — the
starvation the owner predicted, and the same class as the earlier deadlock where a stop waited on a lane that was no longer
serviced. The measured fact is not "the base is absent"; it is **"overlay demand cannot bootstrap its own prerequisite, and
nothing else wakes it"**.

Required shape (design fixed, code not yet written):

1. **The dependency is a committed-lane obligation.** When a slot has an overlay demand whose canonical prefix is not ready,
   the **committed owner** must be offered canonical-prefix grains for that slot even though it has no committed window
   demand of its own. Ownership is unchanged — one driver — but the obligation is *enlivened by the dependency* rather than by
   a request the committed lane itself carries.
2. **The overlay lane only observes.** `WaitingCanonicalBase` stays a pure reading of `(committedRevision,
   canonicalInputGeneration, captureGeneration, coverage)`; a revisit continues waiting, advances to the base fill, or retires
   and restarts — and it never calls `stepCanonicalPrefix`.
3. **The obligation must be bounded and honest.** The wake-up is a *work offer*, not a priority claim: it must not starve the
   committed lane's own demand, persistence, stop/finalization or another sounding slot, and if it can never be granted the
   lane must report that by name rather than wait forever.

So the implementation order becomes: **(0) make the committed owner receive canonical-prefix work under overlay-only demand**
with the liveness fixture as its own RED→GREEN, and only then (1) `WaitingCanonicalBase`, (2) base fill, (3) continuation,
(4) named contention at publication, (5) identity-keyed retirement.

No source line changed; the probe was removed byte-identically (`git diff` clean apart from documentation).

## 2b-ii-b — correction: the liveness RED is **not established** (2026-10-22)

The previous entry measured 400 turns of `Track::stepPlaybackViewMaintenance` and called the empty canonical inputs a RED.
That scope was wrong: the canonical prefix is not driven by the playback-view owner at all. It is driven by
`Track::processDeferredCanonicalPrefix`, reached through the **VisualCache / VisualCacheWrapAppend** deferred turns
(`MidiServiceDrain::serviceCanonicalPrefixTurn`), and gated by
`MidiServiceDrain::canonicalPreparationHasPriorityWork(overdubbing, visualDirty, wrapAppend, sourceSpansReady)`, whose
overdub term is `overdubbing && !sourceSpansReady` — i.e. **during an overdub, canonical preparation is deliberately
deprioritized while the overdub's own source-span cache is not ready.**

Second measurement, same fixture, 400 `processDeferredIdleMaintenance(..., DeferredIdleOwner::VisualCache, 1)` turns:
`ready=0 passCount=0` before **and** after. So neither owner entry makes progress here — but that is a statement about this
**harness shape** (no display/paint work, so the source-span cache never becomes ready and the higher-priority term stays
true), not a verdict on the real rotation.

Therefore:

- **The liveness RED is withdrawn as a claim.** What is established: (a) the playback-view owner does not advance the
  canonical prefix, (b) the VisualCache owner does not in this fixture either, (c) the rule that explains both is the
  deliberate overdub deprioritization above.
- **The liveness question is still owed**, and its precondition is a **harness** fact, not a scheduling one: it must be
  measured where `overdubSourceSpanCacheReady()` can become true (the real `updateAllTracks` rotation, or a fixture that
  drives the source-span/paint work first). Until then neither "it wakes" nor "it starves" may be claimed, and no scheduling
  change is justified by the measurement.
- The owner's design is unaffected and remains the target: the dependency must make the canonical owner *runnable*
  (`CanonicalPrerequisiteOwed(slot)`), arbitration ranks it below stop/finalization and existing committed/sounding work, and
  a prerequisite that cannot legally be serviced must be reported (`CanonicalDependencyDeferred`/`Blocked`) rather than
  waiting indistinguishably.

Both probes were removed byte-identically; the focused suite is back to **91/91** and no source line changed.

## 2b-ii-b — liveness measured GREEN through the real rotation shape (2026-10-22)

Correction applied (previous entry): the canonical prefix is `Track::processDeferredCanonicalPrefix`, offered by
`MidiServiceDrain::serviceCanonicalPrefixTurn` during the **VisualCache** turn and gated by
`canonicalPreparationHasPriorityWork(...)`, whose overdub term (`overdubbing && !sourceSpansReady`) deprioritizes it until the
display/source-span cache is ready. Measured with both prerequisites in place, on a live overdub with **no committed demand of
its own**, using exactly the `main.cpp` pair:

```
spanReady=1  canonicalReady=0  passes=0
after 13 canonical-owner turns:  canonicalReady=1  passes=1
```

So liveness **holds**: a live overlay demand does not need to drive anything, because the canonical owner is already runnable
and advances the base once the paint prerequisite exists. The earlier "RED" was (a) the wrong owner entry and (b) the
deliberate deprioritization while the span cache was absent.

The registered fixture now asserts the three parts the owner asked for, in
`test_3_15e_slice2b_the_overlay_demand_is_the_live_generation_and_the_committed_interval`:

1. `canonicalPassInputs.ready()` after bounded canonical-owner turns under overlay-only demand;
2. the work went through the committed owner's own grains (`serviceCanonicalPrefixTurn` →
   `Track::processDeferredCanonicalPrefix`, `stepped > 0`, `passCount == 1`);
3. the overlay lane issued **zero** canonical work — its only entry is the const `overdubOverlayDemandForSlot` reading, and
   `pendingViewRequest_.active` stays false, so the overlay demand created no committed demand and no second driver.

**Named limitation (honest):** the negative companion — "*the prerequisite must not steal a turn while
`canonicalPreparationHasPriorityWork` is true*" — is **not** demonstrated here: by this point in the fixture the span cache is
already established, so the deprioritized branch is not reachable without a dirty-span overdub state of its own. It is
recorded as owed rather than asserted. The stale-identity case is satisfied **by construction**: the prerequisite is derived
per turn from live state (`overdubOverlayDemandForSlot`), so there is no retained obligation that could survive a revision or
capture-generation change — there is nothing to go stale.

No source line changed (test only). Gates: focused **91/91**, full native **3 400 / 7 skipped / 3 393 succeeded / 0 failed**.

## 2b-ii-b step 1 — the waiting state is a pure observation (2026-10-22)

`include/Utils/OverdubOverlayLane.h` is the lane's vocabulary and nothing more: `Step { NotRequired, WaitingCanonicalBase,
BaseReady, RetireStaleCandidate }` plus the identity quartet a revisit must find unchanged — committed revision, canonical
generation, capture generation, coverage — and a `candidateIdentityMismatch` flag that orders retirement *before* any build.

`observe()` is pure: no canonical work, no view role, no request record. The fixture gathers those inputs from real state on
a live overdub (`canonicalPassInputs.ready()`, `canonicalPassInputs.generation()`, `overdubOverlayDemandForSlot`) and asserts:

- canonical not ready → `WaitingCanonicalBase` and `waiting() == true`;
- observing changed nothing: revision, capture generation, canonical pass count all unchanged, and the demand still owed;
- `candidateIdentityMismatch` → `RetireStaleCandidate` (retirement is ordered before building);
- `baseCommitted == false` → `NotRequired` (a slot that can never satisfy the wait is **not** "waiting"), and no demand →
  `NotRequired`.

Two mutations each kill exactly one assertion and are restored byte-identically (`sha256 d02be9dd1bcaee5d…`): "always
`BaseReady`" fails the waiting case (`Expected 1 Was 2`), and dropping the mismatch branch fails the retirement case
(`Expected 3 Was 1`).

No firmware TU includes the header. Gates: focused **91/91**, full native **3 400 / 7 skipped / 3 393 succeeded / 0 failed**.

**Owed next:** step 2 — begin the provenance-bearing base fill when `BaseReady` (the `RuntimeCompanionPasses` form), then
step 3 the continuation, step 4 the append-stable tail, step 5 a named publication/contention result, step 6 identity-keyed
retirement built on this `RetireStaleCandidate` step.

## 2b-ii-b step 2 — the base fill needs a storage decision first (2026-10-22)

Step 2 is "on `BaseReady`, begin the provenance-bearing base fill". Reading the runtime surface before writing it shows the
fill has exactly one possible source instance per slot and that instance is not free by construction:

| Surface | Owner / use |
|---|---|
| `LoopPlaybackRuntime::frozenCompanionPasses` (one per slot) | the **stopped-oracle** path: built while STOPPED, matched against the oracle, published through `publishStoppedOracleView`, then `discard()`ed |
| `LoopPlaybackRuntime::frozenCompanionApply` (one per slot) | the same path's apply object |
| `RuntimeCanonicalPassInputs canonicalPassInputs` | the committed canonical prefix (liveness measured: ready in 13 owner turns under overlay-only demand) |

There is **no second companion storage** on the runtime, and adding one would move the pinned `sizeof(LoopPlaybackRuntime)`
(`7 904` target / `10 104` host) — a deliberate re-base, which this task is not authorized to make casually. So step 2 must
choose, explicitly:

1. **Documented borrow (preferred, if the invariants allow):** the overlay lane borrows `frozenCompanionPasses` /
   `frozenCompanionApply` while the slot is **OVERDUBBING**, where the oracle path cannot be in flight (it requires STOPPED
   and discards after publishing). This needs a named guard — the lane must refuse (`BaseFillRefused::CompanionBusy`) unless
   `frozenCompanionPasses.idle()` and no oracle/published-tail generation references it — and it must be proven that a later
   STOPPED oracle build cannot collide with a retained overlay candidate.
2. **A second instance with a measured re-base:** cleanest semantically, but it changes the per-slot runtime size pin and the
   RAM1 budget, so it needs its own measurement and an owner decision.
3. **A view-only base** (no companion instance): not available — the `RuntimePlaybackViewFill` baseline overload takes a
   `RuntimeCompanionPasses`; the raw-array path is exactly the shape step 0 refused for missing provenance.

No code was written for this step: choosing (1) or (2) changes either a shared-instance invariant or the RAM1 pin, and both are
exactly the kind of decision that must not be inferred from convenience. The next slice starts by proving the borrow guard in
(1) — `CompanionBusy` reached in a fixture with the oracle in flight, `BaseFillRefused` never silent — or by measuring (2).

No source or test line changed for this entry.

## 2b-ii-b step 2 (a) — the borrow guard is named, and the instance is free while overdubbing (2026-10-22)

Option (1) of the storage decision is now encoded and proved at the decision boundary:

- `Step::BaseFillRefused` joins the vocabulary — canonical ready **and** the one companion instance not borrowable is a
  **named** refusal, never a silent fill and never a wait;
- `Observation::companionAvailable` carries the borrow precondition (`frozenCompanionPasses.idle()` and
  `frozenCompanionApply.available()`), and `observe()` refuses before any build when it is false;
- `waiting()` stays false for `BaseFillRefused`, so a refused fill cannot be reported as progress.

Fixture (in `test_3_15e_slice2b_…`): canonical ready + companion free → `BaseReady`; canonical ready + companion busy →
`BaseFillRefused` (and not "waiting"); and on the real live-overdub state, after the liveness run, the actual
`frozenCompanionPasses.idle()` and `frozenCompanionApply.available()` both hold — i.e. the borrow option is viable in the
state the lane runs in, not only in the abstract.

Mutation (header `sha256 bbacf1020e4d8f26…`, restored byte-identically): ignoring `companionAvailable` fails exactly the
refusal assertion (`Expected 2 Was 3`).

Gates: focused **91/91**, full native **3 400 / 7 skipped / 3 393 succeeded / 0 failed**. No firmware TU includes the header.

**Still owed in step 2:** the fill itself (begin `RuntimeCompanionPasses` from the borrowed instance for the demand's coverage,
step Fill/Order to complete, then `beginLiveOverlayContinuation`), plus the collision proof the borrow needs — a STOPPED
oracle build after a retained overlay candidate must be refused or invalidate the candidate by name rather than reuse the
storage under it.

## 2b-ii-b step 2 (b) — the borrow's collision rule, as a decision the oracle must consult (2026-10-22)

A retained overlay candidate **owns** the borrowed companion storage until it is retired, so the stopped-oracle path may not
begin another companion build underneath it. The rule is now explicit and named
(`OverdubOverlayLane::oracleRebuildDecision`):

| Retention | Decision |
|---|---|
| no overlay candidate | `Allowed` — the oracle borrows as it always has |
| retained, identity current | `RefusedRetainedCandidate` — refused **by name** for this turn |
| retained, identity stale | `RetireCandidateFirst` — retire the candidate (the lane's `RetireStaleCandidate`), then rebuild; never rebuild underneath it and never leave it unreported |

Both facts the decision needs (`overlayCandidateRetained`, `candidateIdentityCurrent`) are the owner's own and are already
derivable from the candidate's `source` identity against the demand; no state is added. The fixture pins all three rows,
including that a stale candidate is **not** overwritten.

Mutation (header `sha256 999611808cf99920…`, restored byte-identically): making the decision unconditional `Allowed` fails
exactly the live-candidate row (`Expected 2 Was 0`).

Gates: focused **91/91**, full native **3 400 / 7 skipped / 3 393 succeeded / 0 failed**. Header only; no firmware TU includes
it, so both images stand.

**Remaining for step 2:** the fill itself (borrowed `RuntimeCompanionPasses` → Fill/Order → `beginLiveOverlayContinuation` →
append-stable tail) and wiring `oracleRebuildDecision` into the oracle path. The wiring is a firmware change and is the first
unit of work that needs both builds and the RAM1 gates again.

## 2b-ii-b step 2 (completion) — the owner build and the oracle collision, wired (2026-10-22; firmware, nothing audible)

Step 2's second half is now production code, still behind `PLAYBACK_WINDOW_ROLLING_ENABLED` and still emitting nothing:

```
overdubOverlayDemandForSlot          arrives only on a live overdub with a committed base
        ↓
observe()                            BaseReady (canonical ready + the one companion instance free)
        ↓
frozenCompanionPasses.begin(...)     the **borrowed** provenance-bearing base (owned companion instance)
        ↓
viewBuild.begin(... PlaybackCoverage) → Fill → Order
        ↓
beginLiveOverlayContinuation(...)    same build identity, append-stable live source
        ↓
frozenCompanionPasses.discard()      the borrow is released as soon as the base is copied (the append half
                                     does not reference it — the continuation nulls the baseline)
        ↓
Fill → Order → retained combined candidate (append frontier extended on each owner turn)
```

The lifecycle is a **stateless** owner turn: `LoopPlaybackRuntime::stepOverdubOverlayBuild` reads its phase from the
builder (`active` / `overlayContinuationActive` / `nextGrain`) and from `frozenCompanionPasses.idle()/ready()`. No field was
added to `LoopPlaybackRuntime`, `Track`, `RuntimePlaybackView` or `RuntimePlaybackViewBuild`; the pinned sizes are the pins
(`7 904` target / `10 104` host, static_asserts unchanged).

**Production entry.** `Track::serviceOverdubOverlayWindow()` runs on the same per-track, off-clock visit as the NOTE_EDIT
preview service (`main.cpp`, the `PlaybackViewMaintenance` owner) and returns a **named** `OverdubOverlayLane::Turn`:
`Idle`, `WaitingCanonicalBase`, `BaseFillRefused`, `Deferred`, `BaseStarted`, `BaseBuilding`, `OverlayStarted`,
`OverlayBuilding`, `CandidateReady`, `RetiredStale`, `Denied`. It observes canonical readiness and never advances it; when
another owner holds the slot's builder or prerequisite workers it **defers** by name (role arbitration is 2b-iii).

**The collision rule, wired.** `publishStoppedCanonicalOracleView` takes the `CandidateRetention` and consults
`oracleRebuildDecision` **before** any companion build or admission term:

| Retention | Behavior |
|---|---|
| none | `Allowed` — the oracle borrows exactly as before (decision reported to the caller) |
| retained, base current | `RefusedRetainedCandidate` — returns before any mutation; the candidate and `current` are untouched; the raw fallback is skipped for exactly this decision |
| retained, base stale | `RetireCandidateFirst` — retires the candidate **inside the call** (detaching its builder), then continues on the existing path |

**The retention rule, corrected by measurement.** `captureContentGeneration()` advances on **every accepted input row**
(`LoopCapture.cpp`, after each append), so it is the **append frontier**, not a pass identity. Retention is therefore keyed
on the **committed base** — the base fill's own provenance source (`passOriginSource`: revision, source generation, snapshot
identity) plus coverage plus "the demand is still owed". A genuinely re-based capture (wrap commit, stop, re-base) is caught
by the builder's own live-source currency (`nextGrain() == Denied`), which retires the candidate through the same path. The
strict `candidateIdentityCurrent` (base **and** live generation) remains available for slice-2 arbitration's claim, but it is
explicitly not the retirement predicate: a candidate one append behind must be extended, not retired.

**One found by this slice's own gates:** the inline candidate predicate was instantiated out of line in the one window-build
TU, landed in ITCM as a weak symbol and pulled a flash→ITCM veneer (**+64 B of ITCM**). Reading the candidate through the
runtime owner (`runtime->overdubOverlayCandidate()`, flash) instead restored the **exact** pins — no re-base, and no new
`FLASHMEM` inline-COMDAT hazard (the class the boot regression already taught us).

### Focused independent review (bounded lifecycle review, `docs/Agents/reviewer.md`)

One focused review was run. Six concrete findings; two were blocking and are fixed and covered by new assertions:

| # | Finding | Repair |
|---|---|---|
| **V1** | The `!isOverdubbing()` early return ran before the boundary branch, but the stop edge already moves the track to `TRACK_OVERDUB_STOPPING` (or PLAYING for a retargeted stop). A stop **mid base-fill** therefore never released the borrowed companion instance — the canonical tail/oracle would wait on this lane. | The boundary branch is now **first**, and the non-overdub branch also releases; new case drives `BaseStarted` → `stopOverdubbing()` → one lane turn and requires `frozenCompanionPasses.idle()`, `frozenCompanionApply.available()`, `!viewBuild.active()`. |
| **V2** | Retirement detached the builder only when `activeDestinationIs`/`completedDestinationIs` held. After a re-based capture `nextGrain()` denies **and clears** the view's flags, so neither held and the builder stayed bound to the retired candidate — every later turn would report `RetiredStale` and never build again. | Detachment now keys on the **binding** (`RuntimePlaybackViewBuild::boundToDestination`). The wrap-retirement case asserts `!overlayContinuationActive()`. |
| R1 | `candidateBaseCurrent` compared revision + coverage only; a snapshot republication at the same revision looked current for a base it no longer held. | The rule now compares the base's **provenance identity** (`passOriginSource`, including the snapshot pointer); the continuation nulls `source`'s snapshot by design, so provenance is the authority. Unit row added. |
| R2 | The by-construction exclusion omitted `completedPromisePendingJudgement` and `ownsCoverageSource()`, both of which sibling admissions include. | Both terms added; the lane defers instead of competing. |
| R3 | The Record-return path's "leftover shell" resets could silently clear a retained candidate. | Both resets now skip the retained candidate (read through the runtime owner). |
| R4 | The new test comment claimed a NoteOn append retires the candidate. | Comment corrected; the append fixture now carries a **live NoteOn pair** through the extension, which is the row the overlay exists to serve. |

The reviewer's own coverage gaps are recorded: it did not build or run the suites (all numbers below are measured here), and
its `RefusedRetainedCandidate` note stands — at STOPPED the demand is no longer owed, so production normally resolves to
`RetireCandidateFirst`; the current-candidate refusal is the fail-closed branch, exercised by the unit fixture.

**Historical decision check (read against the patch):** DEC-060 (canonical per-pass companion application — the borrowed
`RuntimeCompanionPasses` form is the one the fill accepts; unchanged), DEC-066 (companion-row reserve accounting — no new
storage), DEC-069 (wrap commit/revision transition — the trigger the retirement covers), DEC-070 (a retargeted stop keeps
its companion work — V1 is a new route into its raw-only fallback, repaired rather than superseded), DEC-108/DEC-116 (one
window owner, one declared grain per owner turn — respected: the lane observes only, one grain per turn, no new scheduler),
DEC-063 (undo prewarm — untouched). No superseding entry is required; no `3.15e` DEC exists yet by design (slice 3 owns the
emission rule).

### Cases (all through a production path)

| Case | Where | Expected (measured) |
|---|---|---|
| companion unavailable | production entry, canonical window in flight | `Turn::BaseFillRefused`, no candidate, no active builder |
| borrowed base → continuation | production entry on a live overdub | `BaseStarted` … `OverlayBuilding`; retained candidate with 4 committed rows + the live pair, `passOriginsReady`, `canonicalRanksReady == false`, sentinel provenance, base/overlay identity split |
| append-stable tail | same candidate, a new live NoteOn pair | an `OverlayBuilding` grain extends the **same** window (+2 rows, earlier rows once); `candidateBaseCurrent` survives the generation advance |
| current candidate vs oracle | real slot runtime | `RefusedRetainedCandidate`, candidate and `current` untouched |
| coverage / revision / snapshot drift | identity rule (unit + integration) | `candidateBaseCurrent == false` (`mustRetire`) |
| stale candidate vs oracle | unit fixture with the real oracle | `RetireCandidateFirst`, candidate cleared **and builder detached**, oracle then publishes |
| wrap generation/revision change | live overdub through the real wrap | the lane retires the candidate; retired view is not complete; builder detached; next oracle decision `Allowed` |
| stop mid base-fill | live overdub, one lane turn, then `stopOverdubbing()` | the borrow is released, `frozenCompanionApply` available, builder inactive |

### Gates (all measured here)

| Gate | Command | Result |
|---|---|---|
| Focused (runtime/slice) | `pio test -e native -f test_playback_prewarm` | **245 / 245** (244 + the new oracle-collision case) |
| Focused (production lane) | `pio test -e native -f test_shared_capture_finalization` | **92 / 92** (91 + the new stop-mid-base-fill case; the 3.15e fixture extended) |
| Full native | `pio test -e native` | **3 402 / 7 skipped / 3 395 succeeded / 0 failed** (baseline 3 400 / 7 / 3 393) |
| Default build | `pio run -e teensy41-capture-serial` | SUCCESS — `391 688 + 1 528`, **byte-identical to the approved pin** |
| Lane build | `pio run -e teensy41-capture-serial-window-qualification` | SUCCESS — `391 848 + 1 368`, **byte-identical to the approved pin** |
| Runtime size pins | compile-time `static_assert` | `sizeof(LoopPlaybackRuntime)` `7 904` target / `10 104` host, unchanged |
| Spec | `openspec validate windowed-playback-generation --strict` | valid |
| Whitespace | `git diff --check` | clean |
| Device | — | **NOT RUN** (3.15f; nothing is emitted) |

### Mutations (each RED exactly its row, restored byte-identically)

| # | Mutation | Result |
|---|---|---|
| M1 | the oracle ignores `CandidateRetention` (always `Allowed`) | the current-candidate refusal row (`Expected 2 Was 0`) |
| M2 | retirement clears the flags but keeps the binding | the stale-candidate-retired assertion |
| M3 | the identity rule drops coverage | the coverage-mismatch row |
| M4 | the tail never extends (`extendLiveCapture` branch removed) | the append-stable row (no `OverlayBuilding` grain) |
| M5 | `mustRetire` uses `candidateIdentityCurrent` instead of `candidateBaseCurrent` | the generation-drift row — an append would retire the candidate |
| M6 (found by review, then fixed) | the old order (`!isOverdubbing()` before the boundary) | the stop-mid-base-fill case (`the borrow is released`) |
| M7 (found by review, then fixed) | the old currency-based detachment | the wrap-retirement case (`retirement must detach the append-stable builder`) |

### Not claimed / owed

- **Nothing is adopted and nothing is emitted.** The `ActiveCaptureOverdub` echo stays authoritative (slice 3 owns the
  edge, the echo skip and the ledger rule; the DEC belongs there).
- **Role arbitration is 2b-iii.** The lane defers when another owner holds the builder; it does not yet ask the resolver or
  retire a losing `ready` candidate.
- **Slot switching mid-overdub** is out of scope: the lane visits the active slot, and the runtime reset owns the old
  slot's resources.
- **Device evidence is 3.15f.** No physical claim follows from native gates.
- `WaitingCanonicalBase` remains silent at the call site (named outcome only); its reported (`CanonicalDependencyDeferred`)
  form stays with the arbitration slice.

## 2b-iii — attempted, reviewed, and **withdrawn before landing**: the priority inversion has no reachable subject, and the naive form breaks DEC-136 (2026-10-22; no code change)

This entry records a **stop**, not a slice. The next unit after 2b-ii-b is 2b-iii ("lanes ask the resolver; losers return
named outcomes; the winner executes `supersedeReady`/`retireStaleReady`; the committed lane may only be deferred once the
overlay can take the boundary"). The owner build can take the boundary, so the inversion was implemented on a feature tree:

- `Track::playbackDemandDecisionForSlot` gathered the three claims from production state (committed = a retained
  `CommittedSnapshot` request vs the live root; preview = the live NOTE_EDIT session revision vs the lane's frozen
  revision; overlay = the live demand with the candidate's **base** identity as currency);
- `applyPlaybackDemandRoleDecisionForSlot` executed `retireStaleReady`/`supersedeReady` on the `ready` role;
- `committedPlaybackRetryCanProgress` and `requestCommittedPlaybackWindow` deferred an ordinary convergence demand during a
  live overdub (named `DeferredToHigherPriority`), and the overlay lane consulted the same decision.

One focused independent review falsified the slice's premise with reachable production paths. Both findings are **accepted**;
the tree was reverted to `8e58f78a` and no firmware line landed.

**V1 — `CommittedSnapshot` is also the Record-return obligation.** DEC-136's private root-return successor requires
`sourceKind == CommittedSnapshot` (`rootReturnSuccessorIsOwedForSlot`) and is offered during an overdub
(`fillPlaybackHolders`). Nothing forbids starting an overdub while a Record presentation is still retiring
(`presentationOwner != nullptr`). The deferral therefore stopped the **one** committed demand that must not be deferred: the
return window could not be qualified, the presentation could not retire (its release gate needs a published window matching
the current root, which an unpublished Capture-origin overlay cannot provide), and the successor's STOP admission is bounded
by that retirement — the documented "the stop press admitted nothing, the overdub can never stop" class. The role decision
had the same conflation: `readyComplete` was derived from `origin == Committed` alone, so a **Committed-origin** Record-return
(`fromFrozenJob`) or launch successor could be retired as "superseded", while the note-edit lane deliberately refuses to do
that to another owner's successor.

**V2 — ordinary convergence does not survive an overdub.** `serviceRollingPlaybackWindow` cancels its frame and any matching
`CommittedSnapshot` pending request as soon as `!isPlaying()` — i.e. on the first maintenance turn after the overdub starts.
So the deferral's stated subject (ordinary committed convergence) has **no production producer during an overdub**; the only
reachable subject is V1's Record-return demand. The fixture that "proved" the inversion called
`requestCommittedPlaybackWindow` directly on an already-overdubbing track, which no production lane does — the reviewer's
"tests manufacture prerequisites production never supplies" rule, and it hid V1.

**Also found (kept as a placement contract for whoever lands 2b-iii):** `PlaybackDemandArbitration::resolve` was inline, and
calling it from `TrackPlaybackWindowBuild.cpp` (whose default section is ITCM) emitted a **532 B weak ITCM symbol plus a
flash→ITCM veneer** in **both** images: default `391,688 + 1,528` → `392,200 + 1,016`, lane `391,848 + 1,368` → `392,360 +
856` (16 B above the floor). Moving the body out of line and flash-resident restored the exact pins. The next attempt should
start from that placement, not from the inline header.

**What the owner must decide before 2b-iii can land** (this is the formal boundary; the review found an authority conflict
between the 2a priority contract and DEC-136's return obligation):

1. **Keep ordinary convergence alive across an overdub** — do not cancel a genuine convergence demand at overdub start
   (the overlay's window carries its base), and give the pending request an **obligation kind** so the arbitration can
   distinguish ordinary convergence from Record-return/launch obligations. Only then may the committed lane be deferred,
   and the `ready` role decision must bind to that demand's own source/coverage (never "any Committed-origin view").
2. **Or defer 2b-iii until slice 3/adoption creates the competition**, leaving the Record-return and launch routes exactly as
   they are. This is the lower-risk option today; it means the 2a priority contract has no production consumer yet, which
   should be recorded as such rather than implied by a test-only demand.

No claim is made about either option's cost. The evidence above is the input the decision needs; the slice's packet should be
rewritten before code, not after.
