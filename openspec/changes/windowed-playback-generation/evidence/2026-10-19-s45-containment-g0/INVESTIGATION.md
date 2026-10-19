# 4.5 — G0 investigation: production containment / the approved last resort

**Date:** 2026-10-19 · **Branch:** `feature/unified-rolling-playback-path` · **Task:** 4.5 (*DEC-108 §13e–g*) ·
**Type:** read-only architectural discovery (G0). **No firmware line, test, decision, plan or authoritative OpenSpec
artifact changed.** This packet is evidence; it does not choose the solution and does not approve implementation.

Question asked: what does 4.5 still owe, and can *"production containment / the approved last resort"* be wired now?

## Scope traced

4.5's other two clauses are landed (recorded in `docs/Runtime/CURRENT_WORK.md`): the **recovery half** — the lane's
adoption boundary is the production caller that clears a latched fault (`2026-10-06`, `3.7a I10`) — and the
**closure that slid behind the re-anchored cursor**, closed at the serving handoff (`3.7a I9`). What remains is
production containment plus displayed provenance (the display's, RWO **S4e**).

## Current execution path (verified)

| Stage | Site | Flag-gated? |
|---|---|---|
| Fault latched | `Track::noteMissedTraversalForSlot`, `TrackPlaybackWindowBuild.cpp:2248` | **No** |
| Fault cleared #1 | `Track::resolveQualificationPlaybackSourceForTurn`, `:1157` (at its own adoption) | **Yes** — inside `#if PLAYBACK_WINDOW_ROLLING_ENABLED` (guard `:1083`) |
| Fault cleared #2 | `Track::advancePlaybackLagTraversal` `Current` branch, `:2210` | Function ungated; its **only production caller** `:4907` is inside the flag (guard `:4781`) |
| Latch read | `Track::containmentForSlot` `:2370`; `Track::deadlineContractFaultLatchedForSlot` `:2375` | Both have **no production caller** |
| Containment policy | `resolveContinuityContainment(faultLatched, validCoverageRemains)`, `include/RuntimePlaybackView.h:254` | Pure, total, ungated |
| Last resort | `Track::applyLastResortForSlot` `:2340` | **No production caller** |
| Promise deadline misses | `Track::serviceAdmittedDeadlineMisses` `:2256`, called from `main.cpp:489` | **No** (the sibling `serviceRollingPlaybackWindow`, `main.cpp:487`, is the gated one) |

**Correction to the earlier working note:** the earlier trace reported `advancePlaybackLagTraversal` as the *only*
clear site. There are **two** (`:1157` and `:2210`), and **both are flag-reachable only**. The conclusion is unchanged
and, if anything, firmer: the latch is set on a path that runs in the shipping image, and every clear is on a path
that does not.

## Ownership / lifetime map

- Per-slot state: `PlaybackLagState lagState` + `deadlineContractFaultLatched` live on the externally allocated
  `LoopPlaybackRuntime` (`include/TrackPlaybackRuntime.h:552`, `:881`); pins hold at **7 888 B** target / 10 072 B host
  (`src/Track/TrackPlaybackRuntime.cpp:2941`, `:2974`), `sizeof(Loop)==1008`, `sizeof(Track)==384`.
- `containmentForSlot` is `const` and reads only the slot's runtime and its published window — it asserts no ownership.
- `applyLastResortForSlot` is **slot-scoped by construction**: `silenceSlotMidiOutput(slotIndex)` (the per-slot primitive,
  `TrackPlaybackHotPath.cpp:853`) plus `runtime->ledger.clear()` — never the track-wide or global entry point, never the
  transport. The approved shape is already right; only the caller is missing.

## Deferred-work map

- **3.7b** (unchecked) — *Remove the legacy playback emission branches*, the final ladder stage, and explicitly dependent
  on 3.7a **and** 3.15a–f **and** 4.5 itself. Each legacy emission branch's removal trigger is 3.7b.
- **Displayed provenance** (RWO S4e) — owed, unowned here.

## State-transition map and the real obstacle

`containmentForSlot`'s "valid coverage remains" is a **coverage** question about the *published window*
(`publishedPlaybackWindowForSlot(...) && coverage.contains(phase)`, `:2368`), not a route question. The withdrawal of
the first form (commit `eefd90a5`, `evidence/2026-10-06-s45e-lane-containment-recovery`) established why that matters:
the resolver's rejections are exactly the turns another route serves. Confirmed structurally in the hot path
(`TrackPlaybackHotPath.cpp:337–343`):

```
pagedEmission  = fixedView == nullptr && livePair == nullptr && fixedBridge == nullptr && admittedForEmission()
useMergedFallback = fixedView == nullptr && livePair == nullptr && !pagedEmission
```

so a turn whose window route refused still emits from the merged fallback (or a record-return window, or paged
emission). **A per-resolver "no coverage" answer is therefore not a "no serving route" answer**, and silencing on it
would close a sounding that another route is about to play — the withdrawn form's F1.

## Resource / timing consequences

The definitions are `TRACK_COLD_MEM` (FLASH) and cost the **shipping image nothing**: with the flag off they are
referenced only by each other and by tests, so the linker may drop them entirely (UNKNOWN whether it does — not
measured here). The withdrawn wiring's **+144 B of ITCM** came from the *call sites* on the resolver edge, not from
these definitions, so any future wiring must budget for call-site placement, not for the policy.

## Existing validation

- `test_second_miss_latches_a_fault_and_the_last_resort_is_targeted_not_global`
  (`test/test_runtime_working_set_ownership/test_runtime_working_set_ownership.cpp:6155`) — the policy, the latch, no
  reset on a further edit, and `SlotSilenced` **only** when coverage is gone. Fixture-only.
- `test_the_lane_boundary_is_the_production_caller_that_recovers_a_latched_fault` (`:6772`) — drives the real lane to an
  adoption, asserts the clear **and** asserts `last_resort_slot_silence` count **0**, i.e. it *pins the absence* of the
  wiring.
- `test_cross_generation_closures_are_reconciled_and_an_empty_window_silences_nothing` (`:6267`) — 4.5's closure clause.
- **HITL:** none for containment. The qualification image (`PLAYBACK_WINDOW_ROLLING_ENABLED=1`) is over its RAM1 budget
  and cannot be flashed, and the shipping image keeps the flag at 0 — so there is no image on which containment
  behaviour is observable on hardware today. This is a **named blocker for 4.5's device evidence**, not a gap in the
  policy.

## Hidden dependencies (the reason this gate exists)

1. **The shipping image can latch a fault and cannot clear it.** `serviceAdmittedDeadlineMisses` runs unconditionally
   (`main.cpp:489`) off a promise-admission path that is itself ungated (`Track::requestPlaybackViewMaintenance` `:3752`
   and `Track::stepPlaybackViewMaintenance` `:5720` sit behind platform guards only), while both clear sites are
   flag-reachable only. Benign **today** because nothing reads the latch in production — but the moment containment is
   wired, an uncleared latch becomes *permanent* and the last resort would silence a slot indefinitely. **Any wiring
   must land a production clear path in the same image**, or the "approved last resort" becomes an unbounded one.
2. **The "no serving route at all" predicate must be invented, not extended.** No owner answers it today. It is a
   per-turn, cross-route question (fixed/bridge, live pair, paged emission, merged fallback, record-return windows),
   i.e. it belongs beside the route selection, and it is exactly the set 3.7b retires.
3. **3.7b is downstream of 4.5**, so "wait for 3.7b" is not a shortcut: tasks 3.7b's own text requires 3.7a, 3.15a–f,
   **4.5**, F-7/F-8, and the service/timing and compatibility gates.
4. **No device-observable image exists for this clause** (see Existing validation), so 4.5's containment evidence is
   native-only until the qualification image fits its budget again.

## VERIFIED

1. `containmentForSlot` and `applyLastResortForSlot` are implemented, compiled unconditionally, and have **no
   production caller** (only each other and tests).
2. The latch is set at `:2248` on a path that runs with the flag off; **both** clear sites (`:1157`, `:2210`) are
   flag-reachable only.
3. The latch has no production reader: nothing observable changes today.
4. The hot path's route selection confirms a refused window route is still served by another route in the same turn.
5. The last resort is already correctly scoped (per slot, no rebuild, no transport stop).
6. The pins (`7 888`/`10 072`, `1 008`, `384`) and the flag default (0) are as recorded; the qualification image cannot
   be flashed.
7. 4.5's recovery half and closure clause are landed; production containment and displayed provenance are not.

## ASSUMPTIONS

1. That the linker drops the uncalled containment code from the shipping image (plausible, unmeasured).
2. That `requestPlaybackViewMaintenance`/`stepPlaybackViewMaintenance` reaching a positional promise is *reachable* in
   the shipping image — it is ungated in source, but whether an admitted promise actually occurs there was not driven
   in a native run.

## UNKNOWNS

1. Which additional serving routes must the predicate count, in full, and in what order (needs a per-turn enumeration).
2. Whether 3.7b's fallback removal is the intended simplification, or a new owner predicate on the existing routes.
3. Whether any of this is observable over serial on a flashable image (currently no candidate env).

## CONFLICTS

1. **None against authority.** The recorded withdrawal reasoning, the code, and `tasks.md` 3.7b agree. The one
   correction is against *this session's own earlier working note* (one clear site → two), recorded above.

## G0 verdict: **BLOCKED**

Discovery is complete and there is **no unreconciled authority conflict**; the blocker is a **missing owner**, not an
unknown. 4.5's remaining implementation clause cannot be landed as a bounded slice because it needs a decision that
does not exist in the tree:

- **(a)** add an **owner-level "no serving route at all" predicate** beside the route selection and wire containment +
  the last resort to it — new owner, new per-turn work on the MIDI-adjacent path, and a **production clear site** in the
  shipping image (hidden dependency 1); or
- **(b)** make it a **3.7b precondition** — retire the legacy emission branches first, after which "the window route
  refused" and "no route remains" coincide — but 3.7b's own text lists 4.5 as a prerequisite.

Either way the decision is the owner's, and (a) additionally owes a call-site ITCM budget and a native-only evidence
plan. Not proposed here.
