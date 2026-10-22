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
