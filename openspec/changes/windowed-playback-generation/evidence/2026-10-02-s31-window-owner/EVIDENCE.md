# Slice 3.1 — the window-build owner: implemented, natively verified, RAM1 line **unchanged**

**Change:** `windowed-playback-generation` · **Task:** 3.1 (owner first) · **Date:** 2026-10-02
**Branch:** `feature/windowed-playback-generation` · **Packet:**
[`G2-EXECUTION-PACKET.md`](../../G2-EXECUTION-PACKET.md) · **Contract:** DEC-108 item 1,
`ExecutionBounds.md` rows 27–29.

**Status: complete and verified.** The owner, its declared contract and its fixtures landed; four mutations are
red-proved with byte-identical restores; the host is green (full native **2679 cases / 2672 succeeded / 7 skipped /
0 failed**); and the firmware build passes the RAM1 budget guard with the **approved line unchanged**
(`variables 96,192`, padding 1,208, locals 34,880). **No re-base** of `scripts/ram1_budget.json` was needed or made.

---

## What landed

| File | Change |
|---|---|
| `include/Utils/MidiServiceDrain.h` | `DeferredIdleOwner::PlaybackViewMaintenance = 6` (last value), `kDeferredIdleOwnerCount` 6 → 7, and the pure eligibility predicate `playbackViewMaintenanceNeeded(runtimePresent, buildUnfinished)` — the `slotProvisionNeeded` shape: the unfinished build *is* the obligation |
| `include/TrackPlaybackRuntime.h` | `coverageBuildNeedsService()` — `viewBuild.active()`, the shared obligation term (an inline method: no data added) |
| `include/Track.h` | the owner's production surface: `PlaybackViewRequestResult` / `PlaybackViewTurnResult`, `requestPlaybackViewMaintenance(...)`, `hasPendingPlaybackViewMaintenance()`, `stepPlaybackViewMaintenance()`, `playbackViewCoverageContract()`, and the two declared grain quanta |
| `src/Track/TrackPlaybackWindowBuild.cpp` | the owner itself: the declared contract, the request entry (margin gate, typed non-destructive refusals, never allocates), the one-grain turn, and the per-turn session-capture row |
| `src/main.cpp` | the deferred turn, served **before** the stopped/playing split (the `SlotProvision` placement), one grain per admitted turn, nothing published |

**No behaviour changes behind the owner:** no production path calls the request entry yet (the source resolution
and the consumer-facing publication are task 3.2), and the turn publishes, hands off and allocates nothing.

## The declared contract (and what it still owes)

`maximumPhaseAdvancePerTurn` = 768 (one bar), `maximumConsecutiveDeniedTurns` = 3, `requiredFillGrains` = 3,
`requiredOrderGrains` = 3, `requiredPromotionGrains` = 1, `leadTicks` = 768 → **required lead = 22,272 ticks**.
Grain counts derive from the declared two-bar window at the design's ~72 events/bar typical density (144 events)
at the two declared quanta (64 fill / 48 order). The per-turn phase advance is **conservative by construction
because it is not measured**, and the direction is the safe one: a larger declared advance demands a *larger*
lead, so the term can only refuse work, never admit too much. The measured figure is owed (task 6.3's sweeps,
task 3.10's derived envelope) — **no supported-density claim is made here**. The `maximumConsecutiveDeniedTurns`
allowance is an **admission input** (it sizes the required lead); the owner's own turn cannot produce a repeatable
denial run today, because a denied grain is terminal in the current build API (`active()` is false afterwards) —
a repeatable run needs a request that survives a terminal denial, which is task 3.2's.

## Native verification (all green, on this tree)

| Command | Result |
|---|---|
| `pio test -e native -f test_midi_service_drain` | **38/38** (the predicate truth table, plus the owner-rotation fixture rewritten for seven owners) |
| `pio test -e native -f test_runtime_working_set_ownership` | **48/48** (three new owner fixtures) |
| `pio test -e native -f test_playback_prewarm` | **190/190** — the suite the change **cites, not restates**, stays green unchanged (task 3.3's clause) |
| `pio test -e native` (full) | **2679 cases: 7 skipped, 2672 succeeded, 0 failed** (baseline 2675 → +4 new cases) |
| `pio run -e teensy41-capture-serial` | **SUCCESS** — `variables 96,192 / ITCM 393,216 (code 392,008 + padding 1,208) / locals 34,880`, floor 840, **368 B of headroom** |

Fixtures, mapped to the packet's oracle:

- **W-1** `test_playback_view_maintenance_turn_work_is_an_unfinished_build` — the predicate's truth table; the turn
  is not a visual-cache dependent (so a pending wrap append cannot skip it); the new owner is the **last** enum
  value and `kDeferredIdleOwnerCount` agrees with it.
- **W-2/W-3/W-6/W-7** `test_window_owner_serves_one_grain_per_turn_and_publishes_nothing` — the turn count is
  derived from the declared quanta (100 events → 2 fill + 3 order = **exactly 5** turns), so a stacked or skipped
  grain changes the number; the cursor persists (the obligation survives every intermediate turn and is gone on the
  last); the pool is identical across each turn; `isPlaybackMergedMidiEventsReadyForSlot` stays false and
  `playbackRevision` never moves — the owner publishes nothing a consumer reads.
- **W-4** `test_window_owner_refusals_are_named_and_non_destructive` — `NoRuntime` and `BuildInFlight` are returned
  as typed outcomes (the enum value *is* the name; the acceptance surface renders it where it reports the row), and
  the refused request leaves the in-flight obligation intact (it still completes).
- **W-5** `test_window_owner_declares_its_caps_and_the_handoff_margin_is_the_oracle` — the declaration is pinned
  value by value, and the margin is the oracle: admitted **at** the required lead, admitted one tick inside it,
  refused one tick outside it.
- **Rotation control** (`test_midi_service_drain`) — one cycle is now eight turns; every owner but Snapshot gets
  exactly one turn per cycle, which is the control that the new turn neither displaces nor starves the others.

## Red-proofs (W-8) — four mutations, each restored byte-identical

`src/Track/TrackPlaybackWindowBuild.cpp` sha256 `6fdfdfe11c26501c…`, `include/TrackPlaybackRuntime.h` sha256
`638a398c0ee8e8ef…` (verified equal before and after every mutation).

| Mutation | Fixtures that failed (with the assertion's own message) |
|---|---|
| a served grain reports `Idle` | `…serves_one_grain_per_turn…` *"an owed window must be served, not skipped"*; `…refusals…` *Expected 1 Was 0* |
| the declared quantum is ignored (one turn does everything) | `…serves_one_grain_per_turn…` *"the build's cursor must persist across turns"* |
| the declared handoff-margin gate is removed | `…declares_its_caps…` *Expected 4 Was 5* (the just-outside case is no longer refused by name) |
| the obligation is claimed unconditionally | all three owner fixtures (*"an idle slot owes nothing before a request"*, and two *Expected 0 Was 2*) |

## The RAM1 finding this slice produced (and the fix)

The first build of this slice **failed the RAM1 budget guard**: `variables 100,288` against the approved `96,192`
(**+4,096 B**), and the growth's mechanism was not obvious — per-object `.data`/`.bss` across all 401 objects was
**unchanged**, and it was neither the `main.cpp` turn nor the functions' `FLASHMEM` placement.

It is exactly the case the RAM1 rule already records: **a few added `.data` bytes snap the following
`ALIGN(1024)`/`ALIGN(4096)` and cost a whole 4 KB of DTCM with no symbol-size change.** The added `.data` here was
three compiler jump tables (`CSWTCH`) of 16–24 B emitted by the owner's three `switch` statements — `.data` went
77,504 → 78,528 (the +1,024 is alignment) and `.bss` 18,688 → 21,760.

**Fix (measured):** the owner carries **no name table and no jump table** —

- the two `playbackView*ResultName` switches were **removed** (a `switch` emits a jump table; a `const char*` array
  lands in `.data` *and* is what the first attempt produced — it did not help);
- `finishPlaybackViewGrain` uses an if/else chain instead of a `switch`;
- the refusal contract is the **typed outcome enum**, rendered as a name where the row is reported.

**Result:** `variables 96,192` — the approved line, byte for byte — with 368 B of padding headroom. **No re-base.**

**Still owed from this finding** (recorded, not fixed): `padding` is 1,208 against a 840 floor, so ~368 B of new
ITCM code would cross back into a 13th bank and cost 32,768 B — the next author who adds code to an ITCM-resident
TU should read the line, not just the gate. This slice's own additions are `.text` only and cost 0 DTCM bytes.

## Not claimed

- No device evidence, no flash, no stimulus: this slice enables no behaviour observable over serial and makes no
  device claim.
- No capacity, density, `N`, deadline-margin or residency claim; the declared contract's phase-advance term stays
  **unmeasured**.
- Task 2.4's "nothing allocates or blocks on the edge" and task 2.8's red-proof are satisfied *for this slice* by
  W-6/W-8; their own slices keep their own gates.
