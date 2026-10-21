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
