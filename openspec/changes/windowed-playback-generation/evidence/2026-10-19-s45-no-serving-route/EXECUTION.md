# 4.5 — the owner-level "no serving route at all" predicate

**Date:** 2026-10-19 · **Branch:** `feature/unified-rolling-playback-path` · **Task:** 4.5, production containment
(*DEC-108 §13f*; *DEC-146*) · **Type:** firmware, native evidence only.

Follows [`2026-10-19-s45-containment-g0/INVESTIGATION.md`](../2026-10-19-s45-containment-g0/INVESTIGATION.md) (the
blocker) and [`2026-10-19-s45-latch-gating/EXECUTION.md`](../2026-10-19-s45-latch-gating/EXECUTION.md) (DEC-145, the
latch's capability boundary).

## Scope pin

**One statement:** containment is asked — and therefore the approved last resort may be applied — only on a turn where
**no serving route covers the traversal phase**. Not "the window lane refused".

## The predicate (beside route selection)

Route selection in `Track::playCommittedLoopMidi` (`src/Track/TrackPlaybackHotPath.cpp`) offers five alternatives. After
selection:

```
anyServingRouteSelected = fixedBridge != nullptr || fixedView != nullptr || livePair != nullptr || pagedEmission
if (!anyServingRouteSelected)
    containPlaybackTurnIfNoRouteForSlot(slot, currentTick, isActive)     // FLASH
```

`Track::containPlaybackTurnIfNoRouteForSlot` (`src/Track/TrackPlaybackWindowBuild.cpp`, `TRACK_COLD_MEM`) owns the rest,
because only the merged fallback remains and its coverage needs the phase:

| Term | Meaning |
|---|---|
| `!shouldAvoidFullVisualRebuild(len)` | short loop: the merged store is the whole loop, so it covers by construction |
| `mergedWindow.windowLengthTicks > 0 && phase in [start, start+len)` | demoted loop: the catch-all's **own bounded window** covers this phase |
| otherwise | **no route covers** → `applyLastResortForSlot(slot, phase)` |

Then the existing policy decides: `containmentForSlot` still asks the *published-window* coverage question, so a slot
whose coverage remains answers `ServeLastCoherentRevision` and is **left alone**; only a slot with neither route coverage
nor published coverage reaches `LastResortTargetedSilence`. The application stays slot-scoped (`silenceSlotMidiOutput` +
`runtime->ledger.clear()`), rebuilds nothing and never touches the transport.

**Two properties the shape pins:**
- **Availability, not content.** An event-empty source is still a route ("no new events"), so silence can never be
  inferred from an empty window — the sustained-note clause.
- **Coverage, not freshness.** A stale merged window that covers still serves (that *is* "the last coherent revision"),
  so it keeps the slot out of containment exactly like a current window. Only a phase nobody covers is contained.

## Why the first form failed and this one does not

The withdrawn form (`eefd90a5`, review 2026-10-06) judged containment at the **resolver's rejections**. Those rejections
are precisely the turns the retained merged fallback serves, so it would close a sounding another route is about to play
(F1). This predicate asks the cross-route question *after* selection and answers it with coverage, so a refused window
lane is never mistaken for "no route".

## Transition oracle (production path, controllable)

`test/test_runtime_working_set_ownership/test_runtime_working_set_ownership.cpp::test_the_last_resort_is_applied_only_when_no_serving_route_covers_the_phase`
— demoted 64-bar slot (`shouldAvoidFullVisualRebuild` true), fault latched through its own owner (two consecutive misses
via `advancePlaybackLagTraversal`), no published window, no bridge/view/pair/paged, and the merged build refused
(`mergedMidiEventsBuildInProgress = true`) so its window covers nothing. Driven through the real MIDI turn
(`rollingTestTick(track, 1000, false)` → `trackManager.updateAllTracks` → `playCommittedLoopMidi`).

| Case | Expectation | Result |
|---|---|---|
| no route covers | `last_resort_slot_silence` **1**, fault still latched (not a recovery) | PASS |
| control: build allowed, same turn | merged store covers → `last_resort_slot_silence` **0** | PASS |
| RED (production change reverted) | exactly this fixture fails (`Expected 1 Was 0`), nothing else | measured |

The fixture clears the refusal flag **before** its first assertion: a failing assertion aborts the function, and a leaked
flag silently broke two later handoff cases in the first RED run (recorded, because it is exactly the kind of
fixture-state leak that hides a green).

## Budget

| Build | code | padding | headroom above the 840 floor |
|---|---|---|---|
| HEAD (DEC-145) | 392,088 | 1,128 | 288 B |
| first inline predicate | 392,280 | 936 | 96 B (**+192 B**) |
| **landed: cold owner** | **392,216** | **1,000** | **160 B (+128 B)** |

The first inline form computed the phase on the MIDI edge. Moving the projection arithmetic (and the merged-window
coverage test) into the cold owner brought the cost to **+128 B**, below the withdrawn form's +144 B. **No re-base**, and
no unrelated code was shaved.

## Gates

| Gate | Result |
|---|---|
| focused suite | **215 / 215** |
| full native | **3332 cases: 3325 succeeded, 7 skipped, 0 failed** |
| `teensy41-capture-serial` | **SUCCESS**, `variables 96,192 / code 392,216 + padding 1,000 / locals 34,880` |
| `openspec validate --strict`, `git diff --check` | valid, clean |

No upload; the lane-enabled qualification image is still over its own RAM1 budget, so there is no flashable image on which
this path can be observed on hardware.

## DEC-145's gate: deliberately **not** lifted

The owner's constraint for this slice was that the latch gate is lifted only in an image that also gains a legitimate
clear boundary. This slice adds no clear boundary: the two clear sites remain the lane's adoption boundary, and the
default image has none. So `deadlineContractFaultLatched` stays `#if PLAYBACK_WINDOW_ROLLING_ENABLED`. Consequence,
stated plainly: **the predicate is ungated and evaluated in both images, but containment can only *act* in the lane
image**, because only there does a fault exist. That is the honest capability boundary, not an oversight — and it is why
the shipping image's always-selectable merged fallback cannot produce a permanent silence.

## What remains in 4.5

- **Displayed provenance** (RWO **S4e**) — untouched.
- **The device leg** — needs an image that runs the lane and fits RAM1 (6.4/6.5 territory).
- **3.7b** — the legacy emission branches (including the merged catch-all) are untouched; the predicate works *with* them
  rather than requiring their removal, which is why 3.7b stays a separate stage.
- The moving closure inside a long/demoted slot's serving handoff (3.7a/3.14).
