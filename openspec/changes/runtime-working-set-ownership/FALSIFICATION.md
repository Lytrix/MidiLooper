# Falsification review — runtime-working-set-ownership

## Resolution (2026-09-18) — all six answered; F1 and F2 change the plan's shape

| # | Resolution |
|---|---|
| F1 | **Confirmed.** Only single-slot playback exists. Multiple simultaneous slots is a *future* requirement: the end state is that slots behave as **virtual tracks** (target **16 MIDI channels x 8 slots**), playable together whenever set active. Today the working envelope is **8 tracks x 1 sounding slot**; a typical scenario may be 2 per track, but that is a working number, not a limit. The memory law must therefore **derive** the maximum sounding count the budget supports and **not preclude 16/32/48/64**. |
| F2 | **Confirmed and refined.** The real contract is a **readiness deadline on immutability**: after a recording/overdub/edit changes content at a tick, an updated *immutable* state must be ready by the time the current tick moves over that tick again, because playback may only consume immutable events. Forcing that state to be a **whole loop** conflicts with memory; the fix is to change the **unit (interval/window) and give it a deadline**, reusing the resumable small deferred publication machinery already built for this. A published root narrows to the **immutable checkpoint authority for undo/save**. Missed deadlines use the **previous immutable generation for that pass** (rule (a), chosen), and the window size becomes configurable and swept -- bar-based today, toward 1/16 beat (12 ticks) and 1/32 beat (6 ticks) so that queued slot switching at jam quantisation is reachable. |
| F3 | **REOPENED AS A DEFECT (user requirement 2026-09-18): the deny-and-re-press behaviour is the defect this change exists to remove.** One live capture plus one _active_ finalizer is the right capacity model, but a stop refused on capacity must be **latched and self-serviced**, never dropped back on the user: `TrackOverdubLifecycle.cpp` currently reports `successor_stop_denied_capacity` and `reentryDecision("deny", ...)`, keeps the interval recording, and its comment says *"A later stop request is admitted once the predecessor retires"* -- i.e. the user must press again. That is not an accepted contract; the descriptor-ring reasoning below still holds (the ring cannot store stopped intervals), but a **latched request is one small piece of state, not a stored interval**, so the latch needs no new capacity. Acceptance is a single press completing the stop, possibly after the predecessor retires. |
| F4 | **Scope clarified.** The UI shows the first **8 tracks** today and that is what is built against; the end state is **bank switching across 8-track banks** to run **16 MIDI channels**. Requirements are therefore demonstrated at 8 tracks while not precluding 16. |
| F5 | **Agreed.** The role tiers fold into `activeLoopIndex` (+ `CapturePhase` / `TrackState`) rather than forming a second authority. |
| F6 | **Resolved by S2 + G.2** (windowed sizing and non-destructive admission): the acceptance becomes diagnostic once the windowed path and the non-destructive denial contract exist. |

A contract refinement (owner request): the many small resumable owners -- display rebuild, companion/overlap
resolution, snapshot publication, persistence drain, session close, store rewrite, read cursors, the LCR device
gate -- have already converged on the same shape, evidenced by three byte-identical `{Pending, Complete, Denied}`
trios and a shared `required*GrainsUpperBound` convention. What is missing is shared identity (generation),
a declared deadline, one vocabulary and universal cost declaration, so the change now formalises a header-only
contract and requires its new owners to conform -- while explicitly rejecting a framework and a big-bang retrofit.

A visual-tier refinement: the display side already has most of the needed blocks -- a bar-sliced resumable
renderer, a snapshot-identity/revision validity check, draw-source reuse keys, a per-bar overview mask, a compact
note-count primitive, and a preload hook (`TrackManager::prewarmSelectedDisplayVisualCache`) that exists with no
caller. The design is a compact per-bar **envelope** for every slot (~64 B/slot, so the whole library fits) plus a
**capped** rendered detail window, reusing those blocks rather than adding a renderer.

A further refinement (F2 follow-up): a **bounded standby tier** -- a small pre-resolved immutable excerpt per
candidate slot, prepared against the *known* next arrival of the ticks it covers -- makes the readiness
contract scheduleable rather than urgent. Its size is density-dependent (1-bar standby is ~2.5 KB/slot typical,
~140 KB/slot at the representable ceiling), so it is byte-capped with eviction and accounted as its own budget
term, and an all-slots policy waits on S6's descriptor rehydration.

The findings below are kept as the record of what was wrong before these resolutions.

Attempt to break the plan against the current tree. Each finding names the claim it attacks, the evidence,
and what must change. Findings are ranked by whether they falsify the thesis, the envelope, or only the
wording.

**Summary: the thesis survives; the envelope and the budget do not, as written.** Four items must be
resolved before S0 starts, and one of them (F1) is a product-capability decision rather than an edit.

---

## F1 — CRITICAL: "2 sounding slots per track" is not a memory question, it is a missing capability

**Claim attacked.** The envelope enumerates "slots sounding per track: 2" and "up to 32 (2 x 16)", and the
budget multiplies the playback lease by 32 on that basis.

**Evidence.** A track has exactly one active, sounding slot:

- `include/Track.h:473` — `uint8_t activeLoopIndex;  // Which loop slot (0-7) is active`
- `include/Track.h:382-383` — `Loop& getActiveLoop() { return getLoop(activeLoopIndex); }`
- `include/Track.h:335` — *"Per-slot capture state (only activeLoopIndex can be RECORDING/OVERDUBBING)."*
- Playback is built from that one loop: `src/Track/TrackPlaybackWindowBuild.cpp:430`, `:447`
  (`Loop& loop = getActiveLoop();`), and `Track::rebuildPlaybackOrder` takes `getActiveLoop()` plus
  `playbackRuntime.slotIfAllocated(activeLoopIndex)` (`src/Track/TrackPlaybackHotPath.cpp:51-56`).

**Finding.** There is no path by which two slots on one track sound simultaneously. Restating memory
ownership cannot create one: it needs a multi-slot playback path (two sounding sources per track, their
ledger/overlap semantics, and the display/selection model that goes with it). The change contains **no task
for this** — S0-S7 are memory and ownership only — yet the spec states the 2-per-track envelope as the thing
S2 must satisfy. S2 would therefore "prove" a budget for 32 leases that nothing can currently sound.

**What must change.** One of:
1. restate the envelope as the real capability — **1 sounding slot per track** (8 today, 16 if the track
   count changes) — and let windowing answer "regardless of loop length" for that; or
2. add an explicit multi-slot playback workstream to this change (or a sibling change it depends on), so
   "2 per track" has an implementation and an owner; or
3. state 2-per-track as a **target envelope the memory architecture must not preclude**, and instrument it,
   while S2 proves the budget at the achievable count.

This is the single most important finding: it decides whether the budget's 32x multiplier is real.

## F2 — MAJOR: the budget omits a mandatory per-sounding-slot published root

**Claim attacked.** `fixed + 32 x PlaybackWindowLease + LiveCaptureLease + DeferredFinalizationLease +
InteractiveLease + overlap + canonical allowance + reserve <= budget`, with the assertion that "only the
playback term is multiplied by the envelope".

**Evidence.** Windowed playback is served *out of* the whole-loop published root:

- `src/Loop/LoopSnapshotPlayback.cpp:171-183` — `Loop::tryGatherPublishedSnapshotEventsInWindow(...)` begins
  with `const ImmutableLoopSnapshotRef snapshot = publishedSnapshot(); if (!snapshot) ... return false;`
- the only alternatives are `SnapshotWindowGatherPath` values (`:193` `Snapshot`, `:316` `Overlay`) — i.e.
  paths *through* a resident root, not a windowed representation that replaces it;
- measured cost of a resident root per loop: ~325 KB (83 snapshot slabs, ~2.6 MB, for a loaded workspace).

**Finding.** A *sounding* slot requires a resident published root, and the budget has no term for it. The
plan only drops roots for **inactive** slots (S5), so the 32-sounding case carries 32 roots ≈ 10 MB — larger
than the entire budget. The plan's own escape hatch ("a full published root only where an authority
requires one") is defeated here, because the playback authority *does* require one today.

**What must change.** Add the term explicitly (`+ 32 x root` if roots stay whole-loop), or make the snapshot
itself windowed/streamed for active playback — which is a *snapshot-residency* change, not the
canonical-residency change S6 covers, and therefore belongs in S2's scope or in its own gated phase. As
written, the budget is incomplete by roughly an order of magnitude in the case it exists to serve.

## F3 — REOPENED: the capacity denial must be latched, not returned to the user

**Original claim attacked.** "`DeferredFinalizationLease` x 1 ... preserves DEC-067 successor re-entry", with the
cardinality asserted while citing only `CaptureStopJobs::kCapacity = 4`.

**What the trace established (and still holds).** The ring is a descriptor ring whose tail must be free, so it
cannot store a second stopped interval; the ordinary bound is the shared single-instance finalization resources
(`captureStopOwner`, the overdub transfer admission, the canonical capture output, the session-exit handoff),
each producing a named, non-destructive denial that takes no descriptor. A queue of stopped *intervals* would be
a separate capacity/ownership change, because each stored interval needs its sealed raw range, pending
closures, staged pass, session receipt and canonical transfer state -- and there is no proven home for four of
those sets.

**What makes it a defect (user requirement, 2026-09-18).** *"I never want a denied stop or a user to need to
press twice. That's the entire reason to do this openspec."* The capacity model is fine; **the user-visible
consequence is not.** Today a capacity-denied stop is dropped (`successor_stop_denied_capacity`,
`reentryDecision("deny", ...)`) and the interval keeps recording until the user asks again. There is **no latch**:
`hasPendingDeferredOverdubStop()` reflects an *admitted* job, not a remembered request.

**Required behaviour.** A stop that cannot complete at once is **latched** (destination, requested tick,
interval generation) and **self-serviced** when the resources free, completing at the correct boundary with **no
second press**. The same rule applies to record/overdub start, slot/track switching and undo/redo: a request is
never refused to the user, it is accepted and completed as soon as it can be.

**Why the latch fits the capacity finding.** A latched request is **one small piece of state per live interval**
-- and there is one live capture interval at a time -- not a stored stopped interval. It therefore needs none of
the storage the ring cannot provide, and it does not reintroduce the queue model that was correctly rejected.

**Acceptance.** One press. The stop completes (possibly after the predecessor retires), the user never re-issues,
and the retry path is observable (`finalization_lease_stop_denied` becomes a **retry** counter, not a
user-facing denial). The pre-fix counterfactual is the existing `successor_stop_denied_capacity` row.

## F4 — MAJOR: the spec contains a requirement this change cannot satisfy

**Claim attacked.** "The envelope to be served is up to two sounding slots per track across **up to sixteen
tracks**."

**Evidence.** `include/Globals.h:65` — `constexpr uint8_t NUM_TRACKS = 8;`. The change has no task to alter
the track count.

**Finding.** A requirement the change cannot satisfy is a spec defect, even when the proposal flags the gap
as an open decision. Either the track count moves into scope (with its own costs: per-track runtime
structures, display, UI, and the `MAX_LOOPS_PER_TRACK` product) or the requirement must be phrased as
"the architecture must not scale with track count, demonstrated at the current count, with 16 as a target".

## F5 — MODERATE: the role tiers create a second authority for "active"

**Claim attacked.** The COLD / SELECTED / PREPARED-ACTIVE / CAPTURE-ACTIVE / DEFERRED lifecycle.

**Evidence.** The tree already has authorities for this: `activeLoopIndex` (the one active slot per track),
`CapturePhase`, and `TrackState` (EMPTY / ARMED / RECORDING / PLAYING / OVERDUBBING / STOPPED), with
`include/Track.h:335` stating that only `activeLoopIndex` may be RECORDING/OVERDUBBING.

**Finding.** The change introduces a parallel lifecycle without stating how it composes: may a slot be
SELECTED while another slot on the same track is CAPTURE-ACTIVE? Does COLD imply `activeLoopIndex` has
moved? `ARCHITECTURE_RULES.md` requires one owner per concept, so an unresolved second authority is a
defect rather than a detail — and it is exactly the kind of ambiguity that produced the earlier ownership
bugs.

**What must change.** Define the tier transitions in terms of the existing authorities (or state the
transfer explicitly, which is the ownership-transfer protocol), so the tiers are a view of `activeLoopIndex`
/ `CapturePhase` / `TrackState` rather than a competitor.

## F6 — MODERATE: the scaling acceptance cannot be attributed while F1/F2 are open

**Claim attacked.** S7's curve — flat in recorded slots, one step per activation, reuse after deactivation.

**Finding.** With F1 unresolved, the "activation" step's size is undefined (one lease or two?); with F2
unresolved, the step includes an unbudgeted root term that dominates it. The test would therefore pass or
fail on quantities the plan has not costed, which makes it non-diagnostic.

**What must change.** Nothing new: S7 becomes meaningful once F1 and F2 are resolved. Until then it should
be treated as a placeholder rather than a gate.

---

## What survives unchanged

- **The thesis**: working RAM must scale with bounded active roles/windows, not with recorded-slot count.
  Nothing above touches it.
- **Lease identity and generation-qualified release** (F5 concerns where the tiers *live*, not that leases
  need generations).
- **Dirty-generation pinning** (F10/S6.1a) and **cold-save persistence parity** (S5.5) — independent of the
  envelope questions.
- **Non-destructive admission** (G.2, and the stop-denial rule) — if anything F3 strengthens the case for it.
- **Red-first characterisation (S1)** and the measured-slope approach.
- **Role classes as a concept** — F3 affects one cardinality, not the model.

## Consequence for sequencing

S0-S2 cannot be signed off until F1 (envelope restatement or new workstream), F2 (budget completeness) and
F3 (admission guard citation) are resolved, because all three change S2's arithmetic. F4 is a wording/scope
fix. F5 should be resolved in S0 alongside the role-class decision.

The cheapest honest path is to run **S1 at today's capability (1 sounding slot per track)** first: it costs
nothing, produces real per-class numbers, and sizes the omitted root term — which is exactly the input F1
and F2 need.
